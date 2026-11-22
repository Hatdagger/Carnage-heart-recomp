/* Carnage Heart: find the player's disc next to the program.
 *
 * The Windows kit is "unzip, put your disc beside the exe, run it". This runs
 * at the very start of every launch (setup host and the built game), before
 * the launcher or the setup wizard look for a disc. When the remembered disc
 * (disc.cfg beside the exe) is missing, it looks for a Carnage Heart .cue in:
 *
 *   <exe dir>            <exe dir>/disc
 *   and, for the built game under build-release/, the kit folder above it.
 *
 * A .cue whose first track is the Redump size of the USA data track wins;
 * otherwise a .cue with "carnage" in its name whose track file exists. The
 * absolute path is written to disc.cfg beside the exe, which is the file the
 * runtime, the launcher and the setup wizard already read, so the disc is
 * preselected and the player is only asked when nothing was found. The
 * wizard's own verify step still checks the dump before Generate.
 *
 * CH_DISC_AUTODETECT=0 turns this off. */

#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>

#if defined(_WIN32)
#include <windows.h>
#define CH_SEP '\\'
#else
#include <dirent.h>
#include <limits.h>
#include <unistd.h>
#define CH_SEP '/'
#endif

#define CH_PATH 1400
/* game.toml [prepare_disc] known_sizes: Carnage Heart (USA) (Track 1).bin */
#define CH_TRACK1_SIZE 58028544LL

static int ch_is_file(const char* p, long long* size_out) {
#if defined(_WIN32)
    struct _stati64 st;
    if (_stati64(p, &st) != 0) return 0;
    if (!(st.st_mode & _S_IFREG)) return 0;
#else
    struct stat st;
    if (stat(p, &st) != 0) return 0;
    if (!S_ISREG(st.st_mode)) return 0;
#endif
    if (size_out) *size_out = (long long)st.st_size;
    return 1;
}

static int ch_is_sep(char c) { return c == '/' || c == '\\'; }

static void ch_dirname(char* p) {
    size_t n = strlen(p);
    while (n > 0 && !ch_is_sep(p[n - 1])) --n;
    while (n > 1 && ch_is_sep(p[n - 1])) --n;
    p[n] = '\0';
}

static const char* ch_basename(const char* p) {
    const char* b = p;
    for (; *p; ++p)
        if (ch_is_sep(*p)) b = p + 1;
    return b;
}

static int ch_join(char* out, size_t cap, const char* a, const char* b) {
    size_t n = strlen(a);
    int w;
    if (n && ch_is_sep(a[n - 1]))
        w = snprintf(out, cap, "%s%s", a, b);
    else
        w = snprintf(out, cap, "%s%c%s", a, CH_SEP, b);
    return w > 0 && (size_t)w < cap;
}

static int ch_icontains(const char* hay, const char* needle) {
    size_t i, j, hn = strlen(hay), nn = strlen(needle);
    for (i = 0; i + nn <= hn; ++i) {
        for (j = 0; j < nn; ++j)
            if (tolower((unsigned char)hay[i + j]) != tolower((unsigned char)needle[j]))
                break;
        if (j == nn) return 1;
    }
    return 0;
}

static int ch_ends_with_ci(const char* s, const char* suf) {
    size_t n = strlen(s), m = strlen(suf);
    return n >= m && ch_icontains(s + n - m, suf);
}

static int ch_self_exe(char* out, size_t cap, const char* argv0) {
#if defined(_WIN32)
    DWORD n = GetModuleFileNameA(NULL, out, (DWORD)cap);
    if (n > 0 && n < cap) return 1;
#else
    ssize_t n = readlink("/proc/self/exe", out, cap - 1);
    if (n > 0) { out[n] = '\0'; return 1; }
#endif
    if (argv0 && argv0[0]) {
        snprintf(out, cap, "%s", argv0);
        return 1;
    }
    return 0;
}

static void ch_absolute(char* out, size_t cap, const char* in) {
#if defined(_WIN32)
    if (!GetFullPathNameA(in, (DWORD)cap, out, NULL)) snprintf(out, cap, "%s", in);
#else
    char tmp[PATH_MAX];
    if (realpath(in, tmp)) snprintf(out, cap, "%s", tmp);
    else snprintf(out, cap, "%s", in);
#endif
}

/* First FILE "..." entry of a cue, resolved against the cue's folder. */
static int ch_cue_first_track(const char* cue, char* out, size_t cap) {
    char line[1200], dir[CH_PATH];
    FILE* f = fopen(cue, "rb");
    if (!f) return 0;
    snprintf(dir, sizeof(dir), "%s", cue);
    ch_dirname(dir);
    while (fgets(line, sizeof(line), f)) {
        char* p = line;
        char* q;
        while (*p == ' ' || *p == '\t' || (unsigned char)*p == 0xEF ||
               (unsigned char)*p == 0xBB || (unsigned char)*p == 0xBF)
            ++p;
        if (strncmp(p, "FILE", 4) != 0 && strncmp(p, "file", 4) != 0) continue;
        p = strchr(p, '"');
        if (!p) break;
        q = strchr(++p, '"');
        if (!q) break;
        *q = '\0';
        fclose(f);
        if (p[0] == '/' || p[0] == '\\' || (p[0] && p[1] == ':'))
            snprintf(out, cap, "%s", p);
        else if (!ch_join(out, cap, dir, p))
            return 0;
        return 1;
    }
    fclose(f);
    return 0;
}

/* 2 = exact Redump track-1 size, 1 = named Carnage Heart with a track, 0 = no. */
static int ch_score_cue(const char* cue) {
    char track[CH_PATH];
    long long size = 0;
    if (!ch_cue_first_track(cue, track, sizeof(track))) return 0;
    if (!ch_is_file(track, &size)) return 0;
    if (size == CH_TRACK1_SIZE) return 2;
    if (ch_icontains(ch_basename(cue), "carnage")) return 1;
    return 0;
}

static void ch_scan_dir(const char* dir, char* best, size_t cap, int* best_score) {
#if defined(_WIN32)
    char pattern[CH_PATH];
    WIN32_FIND_DATAA fd;
    HANDLE h;
    if (!ch_join(pattern, sizeof(pattern), dir, "*.cue")) return;
    h = FindFirstFileA(pattern, &fd);
    if (h == INVALID_HANDLE_VALUE) return;
    do {
        char cand[CH_PATH];
        int s;
        if (fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) continue;
        if (!ch_join(cand, sizeof(cand), dir, fd.cFileName)) continue;
        s = ch_score_cue(cand);
        if (s > *best_score) { *best_score = s; snprintf(best, cap, "%s", cand); }
    } while (*best_score < 2 && FindNextFileA(h, &fd));
    FindClose(h);
#else
    DIR* d = opendir(dir);
    struct dirent* e;
    if (!d) return;
    while (*best_score < 2 && (e = readdir(d)) != NULL) {
        char cand[CH_PATH];
        int s;
        if (!ch_ends_with_ci(e->d_name, ".cue")) continue;
        if (!ch_join(cand, sizeof(cand), dir, e->d_name)) continue;
        if (!ch_is_file(cand, NULL)) continue;
        s = ch_score_cue(cand);
        if (s > *best_score) { *best_score = s; snprintf(best, cap, "%s", cand); }
    }
    closedir(d);
#endif
}

/* disc.cfg beside the exe names a disc that is still there. */
static int ch_remembered_disc_ok(const char* cfg) {
    char line[CH_PATH];
    size_t n;
    FILE* f = fopen(cfg, "rb");
    if (!f) return 0;
    if (!fgets(line, sizeof(line), f)) { fclose(f); return 0; }
    fclose(f);
    n = strlen(line);
    while (n && (line[n - 1] == '\n' || line[n - 1] == '\r' || line[n - 1] == ' '))
        line[--n] = '\0';
    if (!n) return 0;
    if (ch_is_file(line, NULL)) return 1;
    {   /* relative entries resolve against the exe dir */
        char dir[CH_PATH], abs[CH_PATH];
        snprintf(dir, sizeof(dir), "%s", cfg);
        ch_dirname(dir);
        return ch_join(abs, sizeof(abs), dir, line) && ch_is_file(abs, NULL);
    }
}

int ch_disc_autodetect(const char* argv0, char* found, size_t found_cap) {
    char exe[CH_PATH], exe_dir[CH_PATH], cfg[CH_PATH], best[CH_PATH];
    char dirs[4][CH_PATH];
    int ndirs = 0, i, score = 0;
    const char* off = getenv("CH_DISC_AUTODETECT");
    FILE* f;

    if (found && found_cap) found[0] = '\0';
    if (off && off[0] == '0') return 0;
    if (!ch_self_exe(exe, sizeof(exe), argv0)) return 0;
    ch_absolute(exe_dir, sizeof(exe_dir), exe);
    ch_dirname(exe_dir);
    if (!ch_join(cfg, sizeof(cfg), exe_dir, "disc.cfg")) return 0;
    if (ch_remembered_disc_ok(cfg)) return 0;

    snprintf(dirs[ndirs++], CH_PATH, "%s", exe_dir);
    ch_join(dirs[ndirs++], CH_PATH, exe_dir, "disc");
    if (ch_icontains(ch_basename(exe_dir), "build")) {
        char parent[CH_PATH];
        snprintf(parent, sizeof(parent), "%s", exe_dir);
        ch_dirname(parent);
        snprintf(dirs[ndirs++], CH_PATH, "%s", parent);
        ch_join(dirs[ndirs++], CH_PATH, parent, "disc");
    }

    best[0] = '\0';
    for (i = 0; i < ndirs && score < 2; ++i)
        ch_scan_dir(dirs[i], best, sizeof(best), &score);
    if (score == 0) return 0;

    {
        char abs[CH_PATH];
        ch_absolute(abs, sizeof(abs), best);
        f = fopen(cfg, "wb");
        if (!f) return 0;
        fprintf(f, "%s\n", abs);
        fclose(f);
        fprintf(stderr, "carnageheart: found disc beside the program: %s\n", abs);
        if (found && found_cap) snprintf(found, found_cap, "%s", abs);
    }
    return 1;
}
