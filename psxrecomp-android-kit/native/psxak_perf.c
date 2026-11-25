/* Android frame-time hint (ADPF performance hint, Android 13+).
 *
 * Test 4 on the Y700 Gen 2: ~10 ms of game work per 16.7 ms frame, 56-60 fps
 * with an audio underrun every ~10 s. Android places threads on cores by their
 * recent load; a thread that sleeps every frame can be kept on slower cores.
 * The performance hint session tells the scheduler the frame target (60 Hz)
 * and each frame's work time, so it can move/boost the thread when frames run
 * long. Loaded with dlsym: minSdk is 29 and the API is 33+; on older Android
 * this does nothing.
 *
 * Called from __wrap_SDL_RenderPresent (psxak_present.c) on the game thread
 * before each present. Work = time from the end of the previous present to
 * the start of this one (includes the pacer's short sleep; a slight
 * overstatement only makes the hint more generous).
 *
 * Every 120 frames it logs the average/max work time and the CPU core the
 * game thread ran on. PSX_ANDROID_PERF_HINT=0 turns the hint off (log stays).
 */
#define _GNU_SOURCE
#include <SDL3/SDL.h>
#include <dlfcn.h>
#include <sched.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/types.h>
#include <unistd.h>

/* Bionic declares it only under _GNU_SOURCE, which the force-included
 * psxak_compat.h comes before. */
int sched_getcpu(void);

typedef struct APerformanceHintManager APerformanceHintManager;
typedef struct APerformanceHintSession APerformanceHintSession;
typedef APerformanceHintManager *(*get_manager_fn)(void);
typedef APerformanceHintSession *(*create_session_fn)(APerformanceHintManager *,
                                                      const int32_t *, size_t, int64_t);
typedef int (*report_fn)(APerformanceHintSession *, int64_t);

static int s_state;               /* 0 = not tried, 1 = active, -1 = unavailable/off */
static APerformanceHintSession *s_session;
static report_fn s_report;
static uint64_t s_last_end_ns;
static uint64_t s_sum_ns, s_max_ns;
static unsigned s_frames;
static uint64_t s_last_present_start;

#define TARGET_NS 16666667LL

static void psxak_perf_init(void)
{
    const char *off = getenv("PSX_ANDROID_PERF_HINT");
    if (off && off[0] == '0') {
        s_state = -1;
        printf("android perf: frame-time hint off (PSX_ANDROID_PERF_HINT=0)\n");
        return;
    }
    void *lib = dlopen("libandroid.so", RTLD_NOW);
    get_manager_fn get_manager = lib ? (get_manager_fn)dlsym(lib, "APerformanceHint_getManager") : NULL;
    create_session_fn create = lib ? (create_session_fn)dlsym(lib, "APerformanceHint_createSession") : NULL;
    s_report = lib ? (report_fn)dlsym(lib, "APerformanceHint_reportActualWorkDuration") : NULL;
    APerformanceHintManager *mgr = get_manager ? get_manager() : NULL;
    if (!mgr || !create || !s_report) {
        s_state = -1;
        printf("android perf: frame-time hint not available on this Android version\n");
        return;
    }
    int32_t tid = (int32_t)gettid();
    s_session = create(mgr, &tid, 1, TARGET_NS);
    if (!s_session) {
        s_state = -1;
        printf("android perf: frame-time hint session refused\n");
        return;
    }
    s_state = 1;
    printf("android perf: frame-time hint on (game thread %d, target 16.67 ms)\n", (int)tid);
}

void psxak_frame_begin_present(void)
{
    if (s_state == 0) psxak_perf_init();
    uint64_t now = SDL_GetTicksNS();
    s_last_present_start = now;
    if (!s_last_end_ns) return;
    uint64_t work = now - s_last_end_ns;
    if (work > 1000000000ull) return; /* paused / backgrounded */
    if (s_state == 1) s_report(s_session, (int64_t)work);
    s_sum_ns += work;
    if (work > s_max_ns) s_max_ns = work;
    if (++s_frames >= 120) {
        printf("android perf: work avg=%.2f ms max=%.2f ms over %u frames, cpu core %d\n",
               (double)s_sum_ns / s_frames / 1e6, (double)s_max_ns / 1e6, s_frames,
               sched_getcpu());
        s_frames = 0;
        s_sum_ns = 0;
        s_max_ns = 0;
    }
}

void psxak_frame_end_present(void)
{
    s_last_end_ns = SDL_GetTicksNS();
}
