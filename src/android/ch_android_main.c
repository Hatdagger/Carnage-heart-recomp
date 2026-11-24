/* Android entry point for the Carnage Heart game library (libmain.so).
 *
 * SDLActivity calls SDL_main(). The runtime's real main() (psxrecomp
 * runtime/src/main.cpp, renamed ch_android_real_main by cmake/ch_android.cmake)
 * anchors every file it uses (game.toml, BIOS, settings, memory cards, disc)
 * on the folder its executable lives in, worked out from argv[0]. An app has
 * no such folder, so this wrapper:
 *   1. makes the app's private files directory the game folder (chdir + an
 *      argv[0] inside it). CarnageHeartActivity has already unpacked the APK's
 *      game files there and copied the player's disc into files/disc/.
 *   2. sends stdout/stderr to logcat (tag CarnageHeart) and to
 *      files/carnageheart.log, so a run can be inspected without a PC.
 *   3. passes on the arguments the activity chose (--no-launcher, --disc ...).
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

int ch_android_real_main(int argc, char *argv[]);

/* The OpenGL renderer (translated to GLES 3.1+, ch_android_gles.c) needs an
 * OpenGL ES 3.1+ context and GL_EXT_blend_func_extended (dual-source
 * blending for the PS1 semi-transparency modes). Checked on a throwaway
 * pbuffer context before the game starts, so a GPU without them goes straight
 * to the software renderer. Returns 1 if the GPU renderer can run. */
static int ch_probe_gles(void)
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

static void *ch_log_pump(void *unused)
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
                __android_log_write(ANDROID_LOG_INFO, "CarnageHeart", buf + start);
                start = i + 1;
            }
        }
        if (start == 0 && used == sizeof(buf) - 1) {
            buf[used] = '\0';
            __android_log_write(ANDROID_LOG_INFO, "CarnageHeart", buf);
            used = 0;
        } else if (start > 0) {
            memmove(buf, buf + start, used - start);
            used -= start;
        }
    }
    return NULL;
}

static void ch_redirect_output(const char *files_dir)
{
    char path[1024];
    snprintf(path, sizeof path, "%s/carnageheart.log", files_dir);
    s_log_file = fopen(path, "w");
    setvbuf(stdout, NULL, _IOLBF, 0);
    setvbuf(stderr, NULL, _IONBF, 0);
    if (pipe(s_log_pipe) != 0) return;
    dup2(s_log_pipe[1], STDOUT_FILENO);
    dup2(s_log_pipe[1], STDERR_FILENO);
    pthread_t t;
    if (pthread_create(&t, NULL, ch_log_pump, NULL) == 0) pthread_detach(t);
}

__attribute__((visibility("default")))
int SDL_main(int argc, char *argv[])
{
    const char *files = SDL_GetAndroidInternalStoragePath();
    if (!files) {
        __android_log_print(ANDROID_LOG_ERROR, "CarnageHeart",
                            "no internal storage path: %s", SDL_GetError());
        return 1;
    }
    ch_redirect_output(files);
    if (chdir(files) != 0)
        fprintf(stderr, "chdir(%s) failed: %s\n", files, strerror(errno));
    setenv("PSX_ANDROID", "1", 1);
    /* Speed report in the log every 2 s: time in game code vs. waiting, and
     * how much overlay code ran native vs. interpreted (test APK 3: the game
     * runs at ~75% speed on the Y700 Gen 2). Shared with "SHARE LAST LOG". */
    setenv("PSX_RUNTIME_PERF_DIAG", "1", 0);
    setenv("PSX_RUNTIME_PERF_DIAG_MS", "2000", 0);
    /* fps, emulation busy % and render busy % every 2 s. */
    setenv("PSX_FRAME_REPORT_MS", "2000", 0);
    /* Back button / gesture: a key the game sees (Circle, in pointer_menu.c)
     * instead of closing the app. */
    SDL_SetHint(SDL_HINT_ANDROID_TRAP_BACK_BUTTON, "1");

    char exe[1024];
    snprintf(exe, sizeof exe, "%s/CarnageHeart_Recompiled", files);

    char **args = (char **)calloc((size_t)argc + 1, sizeof(char *));
    if (!args) return 1;
    args[0] = exe;
    for (int i = 1; i < argc; i++) args[i] = argv[i];
    args[argc] = NULL;

    printf("Carnage Heart (Android): game folder %s\n", files);
    for (int i = 1; i < argc; i++) printf("  arg %d: %s\n", i, argv[i]);

    /* PLAY asks for the OpenGL renderer; PLAY (SOFTWARE RENDERER) for software.
     * Without the GPU features it needs, OpenGL becomes software here. */
    int gpu = 0;
    for (int i = 1; i + 1 < argc; i++) {
        if (!strcmp(args[i], "--renderer") && !strcmp(args[i + 1], "opengl")) {
            if (ch_probe_gles()) gpu = 1;
            else args[i + 1] = "software";
        }
    }
    /* The software renderer draws on the CPU; the player setting's default
     * Internal resolution (4x, meant for the OpenGL renderer) made it
     * rasterise 16x the pixels and held the Y700 Gen 2 at ~45 fps with audio
     * underruns (test 3 log). The GPU renderer uses the player setting. */
    if (!gpu) {
        setenv("PSX_INTERNAL_RESOLUTION", "native", 0);
    } else {
        /* 4x (960 lines; 4096x2048 VRAM surface) is sharp on a 1600-line
         * panel. Fixed here so a "Match display" setting carried over from
         * a PC can't ask the tablet GPU for more. */
        setenv("PSX_INTERNAL_RESOLUTION", "4x", 0);
    }
    printf("Carnage Heart (Android): renderer %s\n",
           gpu ? "OpenGL ES (4x internal resolution)" : "software (native resolution)");

    int rc = ch_android_real_main(argc, args);
    printf("Carnage Heart (Android): main returned %d\n", rc);
    fflush(stdout);
    free(args);
    return rc;
}
