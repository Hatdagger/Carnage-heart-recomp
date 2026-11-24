/*
 * Turn files over the internet (multiplayer plan step 6b; built with
 * -DCH_REMOTE_BATTLE=ON). turn_files.c keeps using a local folder; this file
 * keeps that folder in step with the turn relay (relay/worker.js):
 *
 *   - my turn files (A: odd numbers, B: even) are uploaded as soon as they
 *     appear in the folder, and again until the relay has taken them;
 *   - the turn file the game is waiting for is downloaded into the folder
 *     (as .part, then renamed, so the game never reads half a file).
 *
 * When it starts it first catches up: it asks the relay for its newest turn
 * file and downloads every one the folder does not have (all of them on a
 * new computer). Until that has worked once, turn_files.c will not start a
 * new game, so player A on an empty folder never overwrites a game in
 * progress.
 *
 * It runs on its own thread and never holds up the game. Network access goes
 * through the system's own curl (as the launcher's updater does): Windows 10+
 * and desktop Linux ship one, so the game carries no TLS code.
 *
 * Run-time switches (all three turn it on):
 *   CH_TURN_RELAY=<url>    the relay, e.g. https://ch-turns.example.workers.dev
 *   CH_TURN_GAME=<code>    the game code, e.g. K7QX-M2PD
 *   CH_TURN_KEY=<key>      this player's key (from creating or joining the game)
 * plus CH_TURN_ROLE=A|B as before. CH_TURN_DIR is optional with a relay; the
 * default folder is online-turns/<code> beside the game.
 */
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#ifdef _WIN32
#  include <windows.h>
#  include <direct.h>
#else
#  include <pthread.h>
#  include <spawn.h>
#  include <sys/stat.h>
#  include <sys/wait.h>
#  include <time.h>
#  include <unistd.h>
extern char **environ;
#endif

static char g_url[512], g_game[32], g_key[128], g_dir[512];
static int g_role;
static volatile uint32_t g_want;     /* the file the game is waiting for (0: none) */
static volatile int g_started;
static volatile int g_ready;         /* 1: caught up with the relay, -1: could not reach it yet */
static volatile uint32_t g_latest;   /* newest turn file on the relay (when g_ready == 1) */
static volatile uint32_t g_uploaded; /* my files up to here are on the relay */

static void sleep_ms(int ms)
{
#ifdef _WIN32
    Sleep((DWORD)ms);
#else
    struct timespec ts = { ms / 1000, (long)(ms % 1000) * 1000000L };
    nanosleep(&ts, NULL);
#endif
}

/* Run curl with these arguments; returns its exit code (22: HTTP error). */
static int run_curl(const char *const *args)
{
#ifdef _WIN32
    wchar_t exe[MAX_PATH];
    UINT n = GetSystemDirectoryW(exe, MAX_PATH);
    if (n == 0 || n > MAX_PATH - 12) return -1;
    wcscat(exe, L"\\curl.exe");
    char line[4096] = "curl.exe";
    for (int i = 0; args[i]; i++) {
        strncat(line, " \"", sizeof line - strlen(line) - 1);
        strncat(line, args[i], sizeof line - strlen(line) - 1);
        strncat(line, "\"", sizeof line - strlen(line) - 1);
    }
    wchar_t wline[4096];
    MultiByteToWideChar(CP_UTF8, 0, line, -1, wline, 4096);
    STARTUPINFOW si; PROCESS_INFORMATION pi;
    memset(&si, 0, sizeof si); si.cb = sizeof si;
    if (!CreateProcessW(exe, wline, NULL, NULL, FALSE, CREATE_NO_WINDOW, NULL, NULL, &si, &pi))
        return -1;
    WaitForSingleObject(pi.hProcess, INFINITE);
    DWORD code = 1;
    GetExitCodeProcess(pi.hProcess, &code);
    CloseHandle(pi.hThread); CloseHandle(pi.hProcess);
    return (int)code;
#else
    const char *av[32];
    int k = 0;
    av[k++] = "curl";
    for (int i = 0; args[i] && k < 31; i++) av[k++] = args[i];
    av[k] = NULL;
    pid_t pid;
    if (posix_spawnp(&pid, "curl", NULL, NULL, (char *const *)av, environ) != 0) return -1;
    int st = 0;
    if (waitpid(pid, &st, 0) < 0) return -1;
    return WIFEXITED(st) ? WEXITSTATUS(st) : -1;
#endif
}

/* Make a folder and any missing folders above it. */
static void make_dirs(const char *dir)
{
    char p[512];
    snprintf(p, sizeof p, "%s", dir);
    for (char *c = p + 1; ; c++) {
        const char ch = *c;
        if (ch == '/' || ch == '\\' || ch == 0) {
            *c = 0;
#ifdef _WIN32
            _mkdir(p);
#else
            mkdir(p, 0755);
#endif
            *c = ch;
        }
        if (!ch) break;
    }
}

static void path_for(char *out, size_t n, uint32_t seq)
{
    snprintf(out, n, "%s/turn-%06u.cht", g_dir, (unsigned)seq);
}

static int file_exists(const char *p)
{
    FILE *f = fopen(p, "rb");
    if (f) fclose(f);
    return f != NULL;
}

static int is_mine(uint32_t seq) { return (int)((seq & 1u) == 0) == g_role; }

static int upload(uint32_t seq)
{
    char path[600], url[700], at[620], hdr[200];
    path_for(path, sizeof path, seq);
    snprintf(url, sizeof url, "%s/v1/games/%s/turns/%u", g_url, g_game, (unsigned)seq);
    snprintf(at, sizeof at, "@%s", path);
    snprintf(hdr, sizeof hdr, "X-CH-Key: %s", g_key);
    const char *args[] = { "-sSf", "--max-time", "30", "-X", "PUT", "-H", hdr,
                           "-H", "Content-Type: application/octet-stream",
                           "--data-binary", at, "-o",
#ifdef _WIN32
                           "NUL",
#else
                           "/dev/null",
#endif
                           url, NULL };
    const int rc = run_curl(args);
    if (rc == 0)
        fprintf(stderr, "[relay] turn file %u is on the relay\n", (unsigned)seq);
    else
        fprintf(stderr, "[relay] could not upload turn file %u yet (curl %d); will retry\n", (unsigned)seq, rc);
    return rc == 0;
}

static int download(uint32_t seq)
{
    char path[600], part[620], url[700];
    path_for(path, sizeof path, seq);
    snprintf(part, sizeof part, "%s.dl", path);
    snprintf(url, sizeof url, "%s/v1/games/%s/turns/%u", g_url, g_game, (unsigned)seq);
    const char *args[] = { "-sf", "--max-time", "30", "-o", part, url, NULL };
    const int rc = run_curl(args);
    if (rc != 0) { remove(part); return 0; }   /* 22 = not there yet */
    FILE *f = fopen(part, "rb");
    unsigned char h[12] = { 0 };
    const size_t n = f ? fread(h, 1, sizeof h, f) : 0;
    if (f) fclose(f);
    if (n < 12 || memcmp(h, "CHTF", 4) != 0) { remove(part); return 0; }
    remove(path);
    if (rename(part, path) != 0) { remove(part); return 0; }
    fprintf(stderr, "[relay] fetched turn file %u from the relay\n", (unsigned)seq);
    return 1;
}

/* Ask the relay for its newest turn file and fetch every one the folder is
 * missing. Returns 1 when that worked. */
static int catch_up(void)
{
    char url[700], out[600];
    snprintf(url, sizeof url, "%s/v1/games/%s", g_url, g_game);
    snprintf(out, sizeof out, "%s/.relay-status.json", g_dir);
    const char *args[] = { "-sf", "--connect-timeout", "5", "--max-time", "15", "-o", out, url, NULL };
    const int rc = run_curl(args);
    char buf[512] = { 0 };
    FILE *f = rc == 0 ? fopen(out, "rb") : NULL;
    if (f) { const size_t got = fread(buf, 1, sizeof buf - 1, f); buf[got] = 0; fclose(f); }
    remove(out);
    const char *p = strstr(buf, "\"latest\":");
    if (!p) {
        fprintf(stderr, "[relay] could not reach the relay to catch up (curl %d); will retry\n", rc);
        return 0;
    }
    const uint32_t latest = (uint32_t)strtoul(p + 9, NULL, 10);
    for (uint32_t s = 1; s <= latest; s++) {
        char path[600];
        path_for(path, sizeof path, s);
        if (file_exists(path)) continue;
        if (!download(s)) {
            fprintf(stderr, "[relay] could not fetch turn file %u to catch up; will retry\n", (unsigned)s);
            return 0;
        }
    }
    /* My files the relay already has need no upload. */
    uint32_t up = 0;
    for (uint32_t s = 1; s <= latest; s++) if (is_mine(s)) up = s;
    g_uploaded = up;
    g_latest = latest;
    fprintf(stderr, "[relay] caught up: the relay has %u turn file(s)\n", (unsigned)latest);
    return 1;
}

#ifdef _WIN32
static DWORD WINAPI relay_thread(LPVOID arg)
#else
static void *relay_thread(void *arg)
#endif
{
    (void)arg;
    while (!catch_up()) {
        g_ready = -1;
        sleep_ms(3000);
    }
    g_ready = 1;
    for (;;) {
        /* Upload my files the relay does not have yet (oldest first). */
        for (uint32_t s = 1;; s++) {
            char p[600];
            path_for(p, sizeof p, s);
            if (!file_exists(p)) break;
            if (!is_mine(s) || s <= g_uploaded) continue;
            if (!upload(s)) break;
            g_uploaded = s;
        }
        /* Fetch the file the game is waiting for. */
        const uint32_t want = g_want;
        if (want && !is_mine(want)) {
            char p[600];
            path_for(p, sizeof p, want);
            if (!file_exists(p)) download(want);
        }
        sleep_ms(want ? 3000 : 1000);
    }
#ifndef _WIN32
    return NULL;
#endif
}

/* Called by turn_files.c when the mode is switched on. Returns 1 if the relay
 * is in use; *dir_out then holds the local folder to use. */
int ch_relay_start(int role, const char *dir, char *dir_out, size_t dir_n)
{
    const char *u = getenv("CH_TURN_RELAY"), *gm = getenv("CH_TURN_GAME"), *k = getenv("CH_TURN_KEY");
    if (!(u && *u && gm && *gm && k && *k))
        return 0;
    snprintf(g_url, sizeof g_url, "%s", u);
    size_t n = strlen(g_url);
    while (n && g_url[n - 1] == '/') g_url[--n] = 0;
    snprintf(g_game, sizeof g_game, "%s", gm);
    snprintf(g_key, sizeof g_key, "%s", k);
    g_role = role;
    if (dir && *dir)
        snprintf(g_dir, sizeof g_dir, "%s", dir);
    else
        snprintf(g_dir, sizeof g_dir, "online-turns/%s", g_game);
    make_dirs(g_dir);
    snprintf(dir_out, dir_n, "%s", g_dir);
    if (!g_started) {
        g_started = 1;
#ifdef _WIN32
        HANDLE t = CreateThread(NULL, 0, relay_thread, NULL, 0, NULL);
        if (t) CloseHandle(t);
#else
        pthread_t t;
        if (pthread_create(&t, NULL, relay_thread, NULL) == 0) pthread_detach(t);
#endif
    }
    fprintf(stderr, "[relay] game %s at %s, player %c, folder %s\n", g_game, g_url, role ? 'B' : 'A', g_dir);
    return 1;
}

/* The game is now waiting for turn file seq (0: not waiting). */
void ch_relay_want(uint32_t seq) { g_want = seq; }

/* 1: the folder has caught up with the relay; -1: the relay could not be
 * reached yet (still trying); 0: not tried yet. */
int ch_relay_ready(void) { return g_ready; }

/* Wait (at most ms) until the first catch-up has worked or failed once. */
int ch_relay_wait_ready(int ms)
{
    for (int t = 0; g_ready == 0 && t < ms; t += 50) sleep_ms(50);
    return g_ready;
}

/* My turn files up to this number are on the relay. */
uint32_t ch_relay_uploaded(void) { return g_uploaded; }
