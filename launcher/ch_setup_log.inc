/* Carnage Heart: setup log file (cmake/ch_setup_log.cmake).
 *
 * The framework's setup wizard shows only the last error line when a step
 * fails, and the player has no console to read. This file is included at the
 * end of a build-time copy of psxrecomp/host/psxrecomp_codegen_host.c and
 * makes every wizard step write to <game folder>/setup-log.txt:
 *   - a header per step (time, step name, kit version),
 *   - every progress message the wizard shows,
 *   - every line the psxrecomp CLI prints (compiler errors, tracebacks),
 *   - the final error, which also gets "Full log: <path>" added so the
 *     wizard tells the player where to look.
 * The Windows rebuild that runs in a console after the wizard exits writes to
 * the same file through launcher/ch_setup_tee.py.
 *
 * The stock psxrecomp_codegen_host_apply() is renamed ch_stock_codegen_host_apply()
 * in the copy; the one below calls it, then wraps the step callbacks. */

#ifndef CH_VERSION
#  define CH_VERSION "dev"
#endif

#define CH_SETUP_LOG_NAME "setup-log.txt"
#define CH_SETUP_LOG_OLD_NAME "setup-log.old.txt"
#define CH_SETUP_LOG_MAX_BYTES (4L * 1024L * 1024L)

static FILE* g_ch_log;
static char g_ch_log_path[1100];

static int ch_setup_log_path(char* out, size_t cap, const char* name) {
    if (!g_project_root[0])
        return 0;
    int n = snprintf(out, cap, "%s%c%s", g_project_root,
#if defined(_WIN32)
                     '\\',
#else
                     '/',
#endif
                     name);
    return n > 0 && (size_t)n < cap;
}

static FILE* ch_setup_log_file(void) {
    if (g_ch_log)
        return g_ch_log;
    if (!ch_setup_log_path(g_ch_log_path, sizeof(g_ch_log_path), CH_SETUP_LOG_NAME))
        return NULL;
    /* Keep the file small: past the limit the old log moves aside once. */
    FILE* probe = fopen(g_ch_log_path, "rb");
    if (probe) {
        long size = -1;
        if (fseek(probe, 0, SEEK_END) == 0)
            size = ftell(probe);
        fclose(probe);
        if (size > CH_SETUP_LOG_MAX_BYTES) {
            char old_path[1100];
            if (ch_setup_log_path(old_path, sizeof(old_path), CH_SETUP_LOG_OLD_NAME)) {
                remove(old_path);
                rename(g_ch_log_path, old_path);
            }
        }
    }
    g_ch_log = fopen(g_ch_log_path, "a");
    return g_ch_log;
}

static void ch_setup_log_line(const char* line) {
    if (!line || !line[0])
        return;
    FILE* f = ch_setup_log_file();
    if (!f)
        return;
    fputs(line, f);
    fputc('\n', f);
    fflush(f);
}

/* Called from cli_tail_note for every line the psxrecomp CLI prints. JSON
 * progress rows reach the log as "[wizard] ..." through the progress
 * callback, so only raw output (compiler text, tracebacks) is written here. */
static void ch_setup_log_cli(const char* line) {
    if (line && line[0] && line[0] != '{')
        ch_setup_log_line(line);
}

static void ch_setup_log_begin(const char* step) {
    FILE* f = ch_setup_log_file();
    if (!f)
        return;
    char when[64] = "";
    time_t now = time(NULL);
    struct tm* tm_now = localtime(&now);
    if (tm_now)
        strftime(when, sizeof(when), "%Y-%m-%d %H:%M:%S", tm_now);
    fprintf(f, "\n===== %s  %s  (Carnage Heart kit %s, %s) =====\n", when, step,
            CH_VERSION,
#if defined(_WIN32)
            "Windows"
#elif defined(__APPLE__)
            "macOS"
#else
            "Linux"
#endif
            );
    fprintf(f, "Game folder: %s\n", g_project_root);
    fflush(f);
}

/* Logs the outcome. On failure the wizard's message gains the log path. */
static int ch_setup_log_end(int ok, char* err_msg, size_t err_cap) {
    FILE* f = ch_setup_log_file();
    if (!f)
        return ok;
    if (ok) {
        ch_setup_log_line("----- step finished OK -----");
        return ok;
    }
    char line[1400];
    snprintf(line, sizeof(line), "----- STEP FAILED: %s -----",
             (err_msg && err_msg[0]) ? err_msg : "(no message)");
    ch_setup_log_line(line);
    if (err_msg && err_cap > 0 && !strstr(err_msg, CH_SETUP_LOG_NAME)) {
        size_t len = strlen(err_msg);
        if (len < err_cap)
            snprintf(err_msg + len, err_cap - len, "%sFull log: %s",
                     len ? "  " : "", g_ch_log_path);
    }
    return ok;
}

/* Progress messages: forward to the wizard, and log each new message once. */
typedef struct {
    RecompLauncherCPrepareProgressFn fn;
    void* ctx;
    char last[256];
} ChLogProgress;

static void ch_setup_log_progress(void* vctx, float pct, const char* msg) {
    ChLogProgress* p = (ChLogProgress*)vctx;
    if (msg && msg[0] && strncmp(msg, p->last, sizeof(p->last) - 1) != 0) {
        snprintf(p->last, sizeof(p->last), "%s", msg);
        char line[400];
        if (pct >= 0.0f)
            snprintf(line, sizeof(line), "[wizard %3d%%] %s", (int)(pct * 100.0f + 0.5f), msg);
        else
            snprintf(line, sizeof(line), "[wizard] %s", msg);
        ch_setup_log_line(line);
    }
    if (p->fn)
        p->fn(p->ctx, pct, msg);
}

typedef int (*ChStepFn)(const char*, char*, size_t, char*, size_t,
                        RecompLauncherCPrepareProgressFn, void*);
typedef int (*ChToolchainFn)(int, const char*, char*, size_t,
                             RecompLauncherCPrepareProgressFn, void*);

static ChStepFn g_ch_stock_prepare;
static ChStepFn g_ch_stock_rebuild;
static ChStepFn g_ch_stock_pgo;
static ChStepFn g_ch_stock_fmv;
static ChToolchainFn g_ch_stock_toolchain;

static int ch_logged_step(ChStepFn stock, const char* step, const char* disc,
                          char* out, size_t out_cap, char* err, size_t err_cap,
                          RecompLauncherCPrepareProgressFn fn, void* ctx) {
    ChLogProgress p;
    memset(&p, 0, sizeof(p));
    p.fn = fn;
    p.ctx = ctx;
    ch_setup_log_begin(step);
    if (disc && disc[0]) {
        char line[1200];
        snprintf(line, sizeof(line), "Disc: %s", disc);
        ch_setup_log_line(line);
    }
    if (err && err_cap)
        err[0] = '\0';
    int ok = stock(disc, out, out_cap, err, err_cap, ch_setup_log_progress, &p);
    return ch_setup_log_end(ok, err, err_cap);
}

static int ch_logged_prepare(const char* d, char* o, size_t oc, char* e, size_t ec,
                             RecompLauncherCPrepareProgressFn fn, void* ctx) {
    return ch_logged_step(g_ch_stock_prepare, "Generate game code from disc",
                          d, o, oc, e, ec, fn, ctx);
}
static int ch_logged_rebuild(const char* d, char* o, size_t oc, char* e, size_t ec,
                             RecompLauncherCPrepareProgressFn fn, void* ctx) {
    return ch_logged_step(g_ch_stock_rebuild,
#if defined(_WIN32)
                          "Build game (schedule console rebuild)",
#else
                          "Build game",
#endif
                          d, o, oc, e, ec, fn, ctx);
}
static int ch_logged_pgo(const char* d, char* o, size_t oc, char* e, size_t ec,
                         RecompLauncherCPrepareProgressFn fn, void* ctx) {
    return ch_logged_step(g_ch_stock_pgo, "FMV optimize (PGO)", d, o, oc, e, ec, fn, ctx);
}
static int ch_logged_fmv(const char* d, char* o, size_t oc, char* e, size_t ec,
                         RecompLauncherCPrepareProgressFn fn, void* ctx) {
    return ch_logged_step(g_ch_stock_fmv, "FMV timing optimize", d, o, oc, e, ec, fn, ctx);
}

static int ch_logged_toolchain(int download, const char* zip, char* err, size_t err_cap,
                               RecompLauncherCPrepareProgressFn fn, void* ctx) {
    ChLogProgress p;
    memset(&p, 0, sizeof(p));
    p.fn = fn;
    p.ctx = ctx;
    ch_setup_log_begin(download == 2 ? "Update compiler pack"
                       : download ? "Compiler pack (download if missing)"
                                  : "Compiler pack (from zip / cache)");
    if (zip && zip[0]) {
        char line[1200];
        snprintf(line, sizeof(line), "Zip: %s", zip);
        ch_setup_log_line(line);
    }
    if (err && err_cap)
        err[0] = '\0';
    int ok = g_ch_stock_toolchain(download, zip, err, err_cap, ch_setup_log_progress, &p);
    return ch_setup_log_end(ok, err, err_cap);
}

void psxrecomp_codegen_host_apply(RecompLauncherCGameInfo* gi,
                                  const PsxrecompCodegenHostConfig* cfg) {
    ch_stock_codegen_host_apply(gi, cfg);
    if (!gi)
        return;
    if (gi->prepare_with_progress && gi->prepare_with_progress != ch_logged_prepare) {
        g_ch_stock_prepare = gi->prepare_with_progress;
        gi->prepare_with_progress = ch_logged_prepare;
    }
    if (gi->rebuild_with_progress && gi->rebuild_with_progress != ch_logged_rebuild) {
        g_ch_stock_rebuild = gi->rebuild_with_progress;
        gi->rebuild_with_progress = ch_logged_rebuild;
    }
    if (gi->pgo_optimize_with_progress && gi->pgo_optimize_with_progress != ch_logged_pgo) {
        g_ch_stock_pgo = gi->pgo_optimize_with_progress;
        gi->pgo_optimize_with_progress = ch_logged_pgo;
    }
    if (gi->fmv_timing_optimize_with_progress &&
        gi->fmv_timing_optimize_with_progress != ch_logged_fmv) {
        g_ch_stock_fmv = gi->fmv_timing_optimize_with_progress;
        gi->fmv_timing_optimize_with_progress = ch_logged_fmv;
    }
    if (gi->ensure_toolchain_with_progress &&
        gi->ensure_toolchain_with_progress != ch_logged_toolchain) {
        g_ch_stock_toolchain = gi->ensure_toolchain_with_progress;
        gi->ensure_toolchain_with_progress = ch_logged_toolchain;
    }
}
