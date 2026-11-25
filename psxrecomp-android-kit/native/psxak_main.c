/* Android entry point for a PSXRecomp game library (libmain.so).
 *
 * SDLActivity calls SDL_main(). The runtime's real main() (psxrecomp
 * runtime/src/main.cpp, renamed psxak_real_main by cmake/psxak.cmake) anchors
 * every file it uses (game.toml, BIOS, settings, memory cards, disc) on the
 * folder its executable lives in, worked out from argv[0]. An app has no such
 * folder, so this wrapper:
 *   1. makes the app's private files directory the game folder (chdir + an
 *      argv[0] inside it). KitSetupActivity has already unpacked the APK's
 *      game files there and copied the player's disc into files/disc/.
 *   2. sends stdout/stderr to logcat (tag PSXAK_LOG_TAG) and to
 *      files/<PSXAK_LOG_NAME>.log, so a run can be inspected without a PC.
 *   3. picks the renderer: OpenGL ES if the GPU probe passes, else software.
 *   4. passes on the arguments the activity chose (--no-launcher, --disc ...).
 *
 * Per-game values come from android_game.json through compile definitions
 * set by cmake/psxak.cmake:
 *   PSXAK_LOG_TAG, PSXAK_LOG_NAME, PSXAK_EXE_NAME, PSXAK_GAME_NAME,
 *   PSXAK_DEFAULT_SCALE.
 *
 * Games can add their own start-up step by defining psxak_game_start()
 * (weak here): it runs on the SDL thread just before the game's main().
 */
#include <SDL3/SDL.h>
#include <EGL/egl.h>
#include <EGL/eglext.h>
#include <GLES3/gl3.h>
#include <android/log.h>
#include <errno.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#ifndef PSXAK_LOG_TAG
#define PSXAK_LOG_TAG "PSXRecomp"
#endif
#ifndef PSXAK_LOG_NAME
#define PSXAK_LOG_NAME "game"
#endif
#ifndef PSXAK_EXE_NAME
#define PSXAK_EXE_NAME "psx_game"
#endif
#ifndef PSXAK_GAME_NAME
#define PSXAK_GAME_NAME "PSXRecomp game"
#endif
#ifndef PSXAK_DEFAULT_SCALE
#define PSXAK_DEFAULT_SCALE "3x"
#endif

int psxak_real_main(int argc, char *argv[]);
void psxak_watch_start(void);

/* Optional game hook (see the top of this file). */
__attribute__((weak)) void psxak_game_start(void);

/* The OpenGL renderer (translated to GLES 3.1+, psxak_gles.c) needs an
 * OpenGL ES 3.1+ context and GL_EXT_blend_func_extended (dual-source
 * blending for the PS1 semi-transparency modes). Checked on a throwaway
 * pbuffer context before the game starts, so a GPU without them goes straight
 * to the software renderer. Returns 1 if the GPU renderer can run. */
static int psxak_probe_gles(void)
{
    EGLDisplay d = eglGetDisplay(EGL_DEFAULT_DISPLAY);
    if (d == EGL_NO_DISPLAY || !eglInitialize(d, NULL, NULL)) {
        printf("android gles probe: no EGL display\n");
        return 0;
    }
    const EGLint cattr[] = { EGL_RENDERABLE_TYPE, EGL_OPENGL_ES3_BIT_KHR,
                             EGL_SURFACE_TYPE, EGL_PBUFFER_BIT, EGL_NONE };
    EGLConfig cfg;
    EGLint n = 0;
    if (!eglChooseConfig(d, cattr, &cfg, 1, &n) || n < 1) {
        printf("android gles probe: no OpenGL ES 3 config\n");
        return 0;
    }
    const EGLint sattr[] = { EGL_WIDTH, 16, EGL_HEIGHT, 16, EGL_NONE };
    EGLSurface surf = eglCreatePbufferSurface(d, cfg, sattr);
    eglBindAPI(EGL_OPENGL_ES_API);
    const EGLint xattr[] = { EGL_CONTEXT_MAJOR_VERSION_KHR, 3, EGL_CONTEXT_MINOR_VERSION_KHR, 2, EGL_NONE };
    EGLContext ctx = eglCreateContext(d, cfg, EGL_NO_CONTEXT, xattr);
    int ok = 0;
    if (surf != EGL_NO_SURFACE && ctx != EGL_NO_CONTEXT && eglMakeCurrent(d, surf, surf, ctx)) {
        GLint maj = 0, min = 0;
        glGetIntegerv(GL_MAJOR_VERSION, &maj);
        glGetIntegerv(GL_MINOR_VERSION, &min);
        const char *ver = (const char *)glGetString(GL_VERSION);
        const char *gpu = (const char *)glGetString(GL_RENDERER);
        GLint next = 0;
        int dual = 0;
        glGetIntegerv(GL_NUM_EXTENSIONS, &next);
        for (GLint i = 0; i < next; i++) {
            const char *e = (const char *)glGetStringi(GL_EXTENSIONS, (GLuint)i);
            if (e && !strcmp(e, "GL_EXT_blend_func_extended")) dual = 1;
        }
        ok = (maj > 3 || (maj == 3 && min >= 1)) && dual;
        printf("android gles probe: %s / %s; ES %d.%d, GL_EXT_blend_func_extended %s -> %s\n",
               ver ? ver : "?", gpu ? gpu : "?", maj, min, dual ? "yes" : "no",
               ok ? "GPU renderer" : "software renderer");
        eglMakeCurrent(d, EGL_NO_SURFACE, EGL_NO_SURFACE, EGL_NO_CONTEXT);
    } else {
        printf("android gles probe: OpenGL ES 3.2 context failed (0x%x)\n", eglGetError());
    }
    if (ctx != EGL_NO_CONTEXT) eglDestroyContext(d, ctx);
    if (surf != EGL_NO_SURFACE) eglDestroySurface(d, surf);
    return ok;
}

static int s_log_pipe[2] = { -1, -1 };
static FILE *s_log_file;

static void *psxak_log_pump(void *unused)
{
    (void)unused;
    char buf[1024];
    size_t used = 0;
    for (;;) {
        ssize_t n = read(s_log_pipe[0], buf + used, sizeof(buf) - 1 - used);
        if (n <= 0) {
            if (n < 0 && errno == EINTR) continue;
            break;
        }
        if (s_log_file) {
            fwrite(buf + used, 1, (size_t)n, s_log_file);
            fflush(s_log_file);
        }
        used += (size_t)n;
        /* Hand complete lines to logcat; keep a partial line for later. */
        size_t start = 0;
        for (size_t i = 0; i < used; i++) {
            if (buf[i] == '\n') {
                buf[i] = '\0';
                __android_log_write(ANDROID_LOG_INFO, PSXAK_LOG_TAG, buf + start);
                start = i + 1;
            }
        }
        if (start == 0 && used == sizeof(buf) - 1) {
            buf[used] = '\0';
            __android_log_write(ANDROID_LOG_INFO, PSXAK_LOG_TAG, buf);
            used = 0;
        } else if (start > 0) {
            memmove(buf, buf + start, used - start);
            used -= start;
        }
    }
    return NULL;
}

static void psxak_redirect_output(const char *files_dir)
{
    char path[1024];
    snprintf(path, sizeof path, "%s/%s.log", files_dir, PSXAK_LOG_NAME);
    s_log_file = fopen(path, "w");
    setvbuf(stdout, NULL, _IOLBF, 0);
    setvbuf(stderr, NULL, _IONBF, 0);
    if (pipe(s_log_pipe) != 0) return;
    dup2(s_log_pipe[1], STDOUT_FILENO);
    dup2(s_log_pipe[1], STDERR_FILENO);
    pthread_t t;
    if (pthread_create(&t, NULL, psxak_log_pump, NULL) == 0) pthread_detach(t);
}

static int valid_scale(const char *v)
{
    return v && (!strcmp(v, "2x") || !strcmp(v, "3x") || !strcmp(v, "4x"));
}

__attribute__((visibility("default")))
int SDL_main(int argc, char *argv[])
{
    const char *files = SDL_GetAndroidInternalStoragePath();
    if (!files) {
        __android_log_print(ANDROID_LOG_ERROR, PSXAK_LOG_TAG,
                            "no internal storage path: %s", SDL_GetError());
        return 1;
    }
    psxak_redirect_output(files);
    if (chdir(files) != 0)
        fprintf(stderr, "chdir(%s) failed: %s\n", files, strerror(errno));
    setenv("PSX_ANDROID", "1", 1);
    /* Speed report in the log every 2 s: time in game code vs. waiting, and
     * how much overlay code ran native vs. interpreted. */
    setenv("PSX_RUNTIME_PERF_DIAG", "1", 0);
    setenv("PSX_RUNTIME_PERF_DIAG_MS", "2000", 0);
    /* fps, emulation busy % and render busy % every 2 s. */
    setenv("PSX_FRAME_REPORT_MS", "2000", 0);
    /* Back button / gesture: a key the game sees instead of closing the app. */
    SDL_SetHint(SDL_HINT_ANDROID_TRAP_BACK_BUTTON, "1");

    char exe[1024];
    snprintf(exe, sizeof exe, "%s/%s", files, PSXAK_EXE_NAME);

    char **args = (char **)calloc((size_t)argc + 1, sizeof(char *));
    if (!args) return 1;
    args[0] = exe;
    for (int i = 1; i < argc; i++) args[i] = argv[i];
    args[argc] = NULL;

    printf("%s (Android): game folder %s\n", PSXAK_GAME_NAME, files);
    for (int i = 1; i < argc; i++) printf("  arg %d: %s\n", i, argv[i]);

    /* "--android-scale 2x|3x|4x" from the start screen's SHARPNESS button
     * (GPU renderer only). Removed here: the runtime doesn't know it. */
    const char *scale = valid_scale(PSXAK_DEFAULT_SCALE) ? PSXAK_DEFAULT_SCALE : "3x";
    for (int i = 1; i + 1 < argc; i++) {
        if (!strcmp(args[i], "--android-scale")) {
            if (valid_scale(args[i + 1])) scale = args[i + 1];
            for (int j = i; j + 2 <= argc; j++) args[j] = args[j + 2];
            argc -= 2;
            break;
        }
    }
    /* PLAY asks for the OpenGL renderer; PLAY (SOFTWARE RENDERER) for software.
     * Without the GPU features it needs, OpenGL becomes software here. */
    int gpu = 0;
    for (int i = 1; i + 1 < argc; i++) {
        if (!strcmp(args[i], "--renderer") && !strcmp(args[i + 1], "opengl")) {
            if (psxak_probe_gles()) gpu = 1;
            else args[i + 1] = "software";
        }
    }
    if (!gpu) {
        /* The software renderer draws on the CPU: a high internal resolution
         * (meant for the GPU) multiplies its work, so it runs at native. */
        setenv("PSX_INTERNAL_RESOLUTION", "native", 0);
    } else {
        /* Fixed here so a "Match display" setting carried over from a PC
         * can't ask the tablet GPU for more. */
        setenv("PSX_INTERNAL_RESOLUTION", scale, 0);
        /* The runtime's render thread draws a frame while the guest runs the
         * next one (Carnage Heart test 7: fixed battle slowdowns). */
        setenv("PSX_RENDER_THREAD", "1", 0);
        /* Driver vsync on high-refresh panels made each present wait for a
         * panel refresh on top of the 59.94 Hz wall-clock pacer. */
        setenv("PSX_VSYNC", "0", 0);
    }
    printf("%s (Android): renderer %s%s%s\n", PSXAK_GAME_NAME,
           gpu ? "OpenGL ES (" : "software (native resolution)",
           gpu ? scale : "",
           gpu ? " internal resolution, render thread on, vsync off / pacer only)" : "");

    /* Cores, clock, run/wait, thermal and hot guest code every 2 s
     * (psxak_watch.c). PSX_ANDROID_WATCH=0 turns it off. */
    psxak_watch_start();

    if (psxak_game_start) psxak_game_start();

    int rc = psxak_real_main(argc, args);
    printf("%s (Android): main returned %d\n", PSXAK_GAME_NAME, rc);
    fflush(stdout);
    free(args);
    return rc;
}
