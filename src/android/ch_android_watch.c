/* Android "watch" line: what the tablet does to the game thread.
 *
 * Test 7 log (24 Nov, GPU 3x, render thread on): battles fine (60 fps,
 * emu_busy ~70 %, no underruns), but at 232-280 s the game fell to 40 fps
 * with emu_busy 100 %, nothing presented (real_fps 0) and audio underruns.
 * Test 6b showed the same on a still screen (198-463 s, 37.7 fps). The guest
 * made the same number of dispatches per frame as in normal play, yet each
 * frame took ~25 ms of host time instead of ~11 ms. Two explanations:
 *   A. the host slowed down: the game thread was moved to a small core, the
 *      clock dropped, or it waited for a CPU (the screen stopped presenting,
 *      so the system may treat the app as idle);
 *   B. the guest ran a heavier loop inside one function on that screen.
 * This line tells them apart. Every 2 s (with the frame report):
 *   cpu=     cores the game thread ran on (sampled 20x/s), most-used first
 *   mhz=     that core's current / max clock (n/a if the system hides it)
 *   run/wait ms per second the thread ran / sat ready waiting for a core
 *            (/proc/self/task/<tid>/schedstat)
 *   thermal= Android thermal status 0 (none) .. 6 (shutdown), API 30+
 *   idle=    idle-loop skips per second (0 with SPEED BOOST off or when the
 *            game has no wait loop the runtime can skip on this screen)
 *   guest=   last dispatched guest function, sampled 500x/s, top 3 with share
 * PSX_ANDROID_WATCH=0 turns it off. Diagnostic only: reads, never changes
 * anything the game sees. */
#define _GNU_SOURCE
#include <dlfcn.h>
#include <pthread.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>
#include <time.h>
#include <unistd.h>

extern uint32_t g_debug_current_func_addr;
extern uint64_t g_idle_skip_count; /* runtime psx_cycles.c: idle-loop skips so far */

static pid_t s_tid;

static uint64_t now_ns(void)
{
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (uint64_t)ts.tv_sec * 1000000000ull + (uint64_t)ts.tv_nsec;
}

static int read_text(const char *path, char *buf, size_t cap)
{
    FILE *f = fopen(path, "r");
    if (!f) return 0;
    size_t n = fread(buf, 1, cap - 1, f);
    fclose(f);
    buf[n] = '\0';
    return n > 0;
}

/* Field 39 of /proc/<tid>/stat: the CPU the thread last ran on. */
static int thread_cpu(void)
{
    char path[64], buf[1024];
    snprintf(path, sizeof path, "/proc/self/task/%d/stat", (int)s_tid);
    if (!read_text(path, buf, sizeof buf)) return -1;
    const char *p = strrchr(buf, ')');
    if (!p) return -1;
    int field = 2;
    for (p++; *p; p++) {
        if (*p == ' ') {
            field++;
            if (field == 39) return atoi(p + 1);
        }
    }
    return -1;
}

static int thread_schedstat(uint64_t *run_ns, uint64_t *wait_ns)
{
    char path[64], buf[128];
    snprintf(path, sizeof path, "/proc/self/task/%d/schedstat", (int)s_tid);
    if (!read_text(path, buf, sizeof buf)) return 0;
    unsigned long long r = 0, w = 0;
    if (sscanf(buf, "%llu %llu", &r, &w) != 2) return 0;
    *run_ns = r;
    *wait_ns = w;
    return 1;
}

static long cpu_khz(int cpu, const char *which)
{
    char path[128], buf[32];
    snprintf(path, sizeof path, "/sys/devices/system/cpu/cpu%d/cpufreq/%s", cpu, which);
    if (!read_text(path, buf, sizeof buf)) return -1;
    return atol(buf);
}

typedef void *(*thermal_acquire_fn)(void);
typedef int (*thermal_status_fn)(void *);
static void *s_thermal_mgr;
static thermal_status_fn s_thermal_status;

static void thermal_init(void)
{
    void *lib = dlopen("libandroid.so", RTLD_NOW);
    if (!lib) return;
    thermal_acquire_fn acq = (thermal_acquire_fn)dlsym(lib, "AThermal_acquireManager");
    s_thermal_status = (thermal_status_fn)dlsym(lib, "AThermal_getCurrentThermalStatus");
    if (acq && s_thermal_status) s_thermal_mgr = acq();
}

enum { NCPU = 16, NFUNC = 64 };

static void *watch_main(void *unused)
{
    (void)unused;
    thermal_init();
    unsigned cpu_hits[NCPU];
    uint32_t fn_addr[NFUNC];
    unsigned fn_hits[NFUNC];
    unsigned fn_used = 0, fn_other = 0, fn_total = 0, cpu_total = 0;
    memset(cpu_hits, 0, sizeof cpu_hits);
    uint64_t last_run = 0, last_wait = 0, last_idle = g_idle_skip_count;
    thread_schedstat(&last_run, &last_wait);
    uint64_t window_start = now_ns();
    unsigned tick = 0;
    const struct timespec step = { 0, 2000000 }; /* 2 ms = 500 Hz */

    for (;;) {
        nanosleep(&step, NULL);
        tick++;

        uint32_t f = g_debug_current_func_addr;
        unsigned i;
        for (i = 0; i < fn_used && fn_addr[i] != f; i++) {}
        if (i < fn_used) fn_hits[i]++;
        else if (fn_used < NFUNC) { fn_addr[fn_used] = f; fn_hits[fn_used++] = 1; }
        else fn_other++;
        fn_total++;

        if (tick % 25 == 0) { /* 20 Hz */
            int c = thread_cpu();
            if (c >= 0 && c < NCPU) { cpu_hits[c]++; cpu_total++; }
        }

        uint64_t t = now_ns();
        if (t - window_start < 2000000000ull) continue;
        const double secs = (double)(t - window_start) * 1e-9;

        char cpus[96] = "";
        int top_cpu = -1;
        for (int k = 0; k < 3; k++) {
            int best = -1;
            for (int c = 0; c < NCPU; c++)
                if (cpu_hits[c] && (best < 0 || cpu_hits[c] > cpu_hits[best])) best = c;
            if (best < 0) break;
            if (top_cpu < 0) top_cpu = best;
            char one[32];
            snprintf(one, sizeof one, "%s%d:%u%%", k ? "," : "", best,
                     cpu_total ? 100u * cpu_hits[best] / cpu_total : 0u);
            strncat(cpus, one, sizeof cpus - strlen(cpus) - 1);
            cpu_hits[best] = 0;
        }

        char mhz[48] = "n/a";
        if (top_cpu >= 0) {
            long cur = cpu_khz(top_cpu, "scaling_cur_freq");
            long max = cpu_khz(top_cpu, "cpuinfo_max_freq");
            if (cur > 0 && max > 0) snprintf(mhz, sizeof mhz, "%ld/%ld", cur / 1000, max / 1000);
            else if (max > 0) snprintf(mhz, sizeof mhz, "?/%ld", max / 1000);
        }

        uint64_t run = 0, wait = 0;
        char rw[64] = "run=n/a";
        if (thread_schedstat(&run, &wait)) {
            snprintf(rw, sizeof rw, "run=%.0f wait=%.0f ms/s",
                     (double)(run - last_run) * 1e-6 / secs,
                     (double)(wait - last_wait) * 1e-6 / secs);
            last_run = run;
            last_wait = wait;
        }

        int thermal = (s_thermal_mgr && s_thermal_status) ? s_thermal_status(s_thermal_mgr) : -1;

        char guest[160] = "";
        for (int k = 0; k < 3; k++) {
            int best = -1;
            for (unsigned j = 0; j < fn_used; j++)
                if (fn_hits[j] && (best < 0 || fn_hits[j] > fn_hits[best])) best = (int)j;
            if (best < 0) break;
            char one[40];
            snprintf(one, sizeof one, "%s0x%08X %u%%", k ? ", " : "", fn_addr[best],
                     fn_total ? 100u * fn_hits[best] / fn_total : 0u);
            strncat(guest, one, sizeof guest - strlen(guest) - 1);
            fn_hits[best] = 0;
        }

        const uint64_t idle = g_idle_skip_count;
        printf("android watch: cpu=%s mhz=%s %s thermal=%d idle=%.0f/s guest=%s\n",
               cpus[0] ? cpus : "?", mhz, rw, thermal,
               (double)(idle - last_idle) / secs, guest[0] ? guest : "?");
        last_idle = idle;

        memset(cpu_hits, 0, sizeof cpu_hits);
        fn_used = fn_other = fn_total = cpu_total = 0;
        window_start = t;
    }
    return NULL;
}

/* Called on the thread that runs the game (SDL_main), before the runtime. */
void ch_android_watch_start(void)
{
    const char *off = getenv("PSX_ANDROID_WATCH");
    if (off && off[0] == '0') return;
    s_tid = gettid();
    pthread_t t;
    if (pthread_create(&t, NULL, watch_main, NULL) == 0) {
        pthread_detach(t);
        printf("android watch: on (game thread %d; cores, clock, run/wait, thermal, hot guest code every 2 s)\n",
               (int)s_tid);
    }
}
