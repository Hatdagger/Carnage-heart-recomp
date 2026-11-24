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
#include <android/log.h>
#include <errno.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

int ch_android_real_main(int argc, char *argv[]);

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

    int rc = ch_android_real_main(argc, args);
    printf("Carnage Heart (Android): main returned %d\n", rc);
    fflush(stdout);
    free(args);
    return rc;
}
