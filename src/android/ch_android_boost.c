/* Android "speed boost": keep the game thread on the fastest core.
 *
 * The game's own code runs on one thread (the PS1 had one CPU), so that
 * thread's speed is the limit on slower devices. Phone chips mix core types:
 * a Snapdragon 865 (Retroid Pocket 5) has one prime core at 2.84 GHz, three
 * at 2.42 GHz and four small ones at 1.8 GHz, and Android does not always
 * keep a busy thread on the prime core. With the boost on:
 *   1. the cores with the highest top clock are found
 *      (/sys/devices/system/cpu/cpuN/cpufreq/cpuinfo_max_freq);
 *   2. three seconds after the game starts (once the runtime has made its
 *      render and audio threads) the game thread is pinned to those cores;
 *   3. every 2 s, any other thread that inherited the pin (a thread the game
 *      thread made later) is given all cores back, so helpers never crowd
 *      the fast core;
 *   4. if the game thread then waits for its core more than 100 ms per
 *      second for three checks in a row (something else is using it), the
 *      pin is removed for the rest of the run.
 * Idle skip, the other half of the boost, is switched on in
 * ch_android_main.c (PSX_IDLE_SKIP).
 *
 * CH_ANDROID_BOOST=0 (the start screen's SPEED BOOST: OFF) turns both off.
 * The game sees nothing different: this only chooses where the thread runs.
 * The "android watch" line (cpu=, mhz=, run/wait) shows the effect. */
#define _GNU_SOURCE
#include <dirent.h>
#include <pthread.h>
#include <sched.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>
#include <time.h>
#include <unistd.h>

#include "ch_android_boost.h"

enum { BOOST_MAX_CPU = 64 };

int ch_boost_pick_fast(const long *max_khz, const unsigned char *allowed, int n,
                       unsigned char *fast)
{
    long best = 0;
    int n_allowed = 0, n_fast = 0;
    memset(fast, 0, (size_t)n);
    for (int c = 0; c < n; c++) {
        if (!allowed[c]) continue;
        n_allowed++;
        if (max_khz[c] <= 0) return 0; /* a core we cannot rate: leave it alone */
        if (max_khz[c] > best) best = max_khz[c];
    }
    if (n_allowed < 2 || best <= 0) return 0;
    for (int c = 0; c < n; c++) {
        if (allowed[c] && max_khz[c] == best) { fast[c] = 1; n_fast++; }
    }
    if (n_fast == n_allowed) { /* every core is the same speed */
        memset(fast, 0, (size_t)n);
        return 0;
    }
    return n_fast;
}

static pid_t s_game_tid;
/* One watcher per game start: an older one stops when the game is started
 * again in the same app process. */
static volatile int s_run;
static cpu_set_t s_all, s_fast;
static char s_fast_text[96];

static long cpu_max_khz(int cpu)
{
    char path[128], buf[32];
    snprintf(path, sizeof path, "/sys/devices/system/cpu/cpu%d/cpufreq/cpuinfo_max_freq", cpu);
    FILE *f = fopen(path, "r");
    if (!f) return -1;
    size_t n = fread(buf, 1, sizeof buf - 1, f);
    fclose(f);
    buf[n] = '\0';
    return n ? atol(buf) : -1;
}

static int game_wait_ns(uint64_t *wait_ns)
{
    char path[64], buf[128];
    snprintf(path, sizeof path, "/proc/self/task/%d/schedstat", (int)s_game_tid);
    FILE *f = fopen(path, "r");
    if (!f) return 0;
    size_t n = fread(buf, 1, sizeof buf - 1, f);
    fclose(f);
    buf[n] = '\0';
    unsigned long long r = 0, w = 0;
    if (sscanf(buf, "%llu %llu", &r, &w) != 2) return 0;
    *wait_ns = w;
    return 1;
}

/* Threads the game thread made after the pin start with its core list.
 * Give those all cores back. Returns how many were changed. */
static int release_helpers(void)
{
    DIR *d = opendir("/proc/self/task");
    if (!d) return 0;
    int changed = 0;
    struct dirent *e;
    while ((e = readdir(d)) != NULL) {
        int tid = atoi(e->d_name);
        if (tid <= 0 || tid == (int)s_game_tid) continue;
        cpu_set_t m;
        CPU_ZERO(&m);
        if (sched_getaffinity(tid, sizeof m, &m) != 0) continue;
        if (CPU_EQUAL(&m, &s_fast) && sched_setaffinity(tid, sizeof s_all, &s_all) == 0)
            changed++;
    }
    closedir(d);
    return changed;
}

static void *boost_main(void *unused)
{
    (void)unused;
    const int run = s_run;
    sleep(3);
    if (run != s_run) return NULL;
    if (sched_setaffinity(s_game_tid, sizeof s_fast, &s_fast) != 0) {
        printf("android boost: could not pin the game thread to core(s) %s; left as it was\n",
               s_fast_text);
        return NULL;
    }
    printf("android boost: game thread %d pinned to the fastest core(s) %s\n",
           (int)s_game_tid, s_fast_text);

    uint64_t last_wait = 0;
    int have_wait = game_wait_ns(&last_wait);
    int crowded = 0;
    for (;;) {
        sleep(2);
        if (run != s_run) return NULL;
        int freed = release_helpers();
        if (freed) printf("android boost: gave %d helper thread(s) all cores back\n", freed);
        uint64_t wait = 0;
        if (!have_wait) continue;
        if (!game_wait_ns(&wait)) return NULL; /* the game thread has ended */
        const uint64_t waited_ms_per_s = (wait - last_wait) / 2000000ull;
        last_wait = wait;
        crowded = waited_ms_per_s > 100 ? crowded + 1 : 0;
        if (crowded >= 3) {
            sched_setaffinity(s_game_tid, sizeof s_all, &s_all);
            release_helpers();
            printf("android boost: the fast core is busy (game thread waited %llu ms/s); pin removed\n",
                   (unsigned long long)waited_ms_per_s);
            return NULL;
        }
    }
    return NULL;
}

int ch_android_boost_enabled(void)
{
    const char *e = getenv("CH_ANDROID_BOOST");
    return !(e && e[0] == '0');
}

/* Called on the thread that runs the game (SDL_main), before the runtime. */
void ch_android_boost_start(void)
{
    s_run++;
    if (!ch_android_boost_enabled()) {
        printf("android boost: off (SPEED BOOST: OFF)\n");
        return;
    }
    s_game_tid = gettid();
    CPU_ZERO(&s_all);
    CPU_ZERO(&s_fast);
    if (sched_getaffinity(0, sizeof s_all, &s_all) != 0) {
        printf("android boost: cannot read the allowed cores; not pinning\n");
        return;
    }
    long khz[BOOST_MAX_CPU];
    unsigned char allowed[BOOST_MAX_CPU], fast[BOOST_MAX_CPU];
    for (int c = 0; c < BOOST_MAX_CPU; c++) {
        allowed[c] = CPU_ISSET(c, &s_all) ? 1 : 0;
        khz[c] = allowed[c] ? cpu_max_khz(c) : 0;
    }
    if (!ch_boost_pick_fast(khz, allowed, BOOST_MAX_CPU, fast)) {
        printf("android boost: no core is faster than the rest (or the clocks are hidden); not pinning\n");
        return;
    }
    s_fast_text[0] = '\0';
    for (int c = 0; c < BOOST_MAX_CPU; c++) {
        if (!fast[c]) continue;
        CPU_SET(c, &s_fast);
        char one[32];
        snprintf(one, sizeof one, "%s%d (%ld MHz)", s_fast_text[0] ? ", " : "", c, khz[c] / 1000);
        strncat(s_fast_text, one, sizeof s_fast_text - strlen(s_fast_text) - 1);
    }
    pthread_t t;
    if (pthread_create(&t, NULL, boost_main, NULL) == 0) {
        pthread_detach(t);
        printf("android boost: on; will pin the game thread to core(s) %s in 3 s\n", s_fast_text);
    }
}
