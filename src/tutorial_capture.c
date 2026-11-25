/* Carnage Heart: tutorial picture capture.
 *
 * The tutorials (tutorials/index.html) show screenshots of the game. They are
 * never shipped: the launcher's INSTALL TUTORIALS step runs the player's own
 * build, from their own disc, in a hidden window and saves the pictures on
 * their PC (tutorials/make_pictures.py starts it).
 *
 * Switched on only when CH_TUTORIAL_CAPTURE names a script file; then
 * CH_TUTORIAL_OUT is the folder for the PNGs and capture-log.txt. Without the
 * variable nothing here runs. The script drives port 1 instead of the real
 * controller, one game PadRead (frame) at a time. One command per line, '#'
 * starts a comment:
 *
 *   wait N              N frames with nothing pressed
 *   press B[+B..] [H]   hold buttons H frames (default 3), then 10 free frames.
 *                       Buttons: up down left right cross circle square
 *                       triangle start select l1 r1 l2 r2
 *   point X Y           mouse cursor to X,Y (game screen, 512 x 240), 4 frames
 *   click X Y           point, then press Cross with the mouse cursor there
 *   screen RA [T]       wait (at most T frames, default 1800) for the menu
 *                       screen whose hotspot call returns to RA (hex), see
 *                       src/screen_labels.c; on timeout the capture stops
 *   shot NAME           save the next presented frame as NAME.png
 *   quit                close the game (also at the end of the file)
 *
 * The game's own arrow is hidden while the mouse cursor is used, so pictures
 * taken after point / click show no arrow. */
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "cpu_state.h"
#include "tutorial_capture.h"

#if defined(PSX_SDL3)
#include <SDL3/SDL.h>
#endif

#if defined(_WIN32)
#include <process.h>
#define ch_exit_now _exit
#else
#include <unistd.h>
#define ch_exit_now _exit
#endif

extern int present_shot_request(const char *path);
extern int present_shot_seq(void);
extern int present_shot_ok(void);
/* GL: a frame equal to the last one is not presented again; this forces the
 * next few presents so a still menu can be saved too. */
extern void gl_renderer_invalidate_present(void);

#define PAD_BUTTONS   0x800E0134u
#define MOUSE_MODE    0x800DF358u
#define CURSOR_X      0x800DF368u
#define CURSOR_Y      0x800DF36Cu

enum { ST_OFF = -1, ST_IDLE = 0, ST_WAIT, ST_PRESS, ST_POINT, ST_CLICK, ST_SCREEN, ST_SHOT, ST_QUIT };

static int      st_state;          /* ST_OFF until the env var is seen */
static int      st_inited;
static FILE    *st_script, *st_log;
static char     st_out[400];
static int      st_left;           /* frames left in this step */
static int      st_phase;
static uint32_t st_buttons, st_target_ra;
static int      st_hold, st_mouse, st_x = 256, st_y = 120;
static int      st_shot_seq, st_shots, st_failed, st_frames, st_line_no;
static char     st_name[96];

static void logf_line(const char *fmt, const char *a, long b) {
    if (!st_log) return;
    fprintf(st_log, "[frame %d, line %d] ", st_frames, st_line_no);
    fprintf(st_log, fmt, a, b);
    fputc('\n', st_log);
    fflush(st_log);
}

static void init_once(void) {
    st_inited = 1;
    st_state = ST_OFF;
    const char *s = getenv("CH_TUTORIAL_CAPTURE");
    const char *o = getenv("CH_TUTORIAL_OUT");
    if (!s || !*s || !o || !*o) return;
    st_script = fopen(s, "r");
    if (!st_script) return;
    snprintf(st_out, sizeof st_out, "%s", o);
    char lp[460];
    snprintf(lp, sizeof lp, "%s/capture-log.txt", st_out);
    st_log = fopen(lp, "w");
    logf_line("capture started: %s%.0ld", s, 0);
    st_state = ST_IDLE;
}

int ch_capture_enabled(void) {
    if (!st_inited) init_once();
    return st_state != ST_OFF;
}

static uint32_t button_bits(const char *list) {
    static const struct { const char *n; uint32_t b; } names[] = {
        {"l2", 0x0001}, {"r2", 0x0002}, {"l1", 0x0004}, {"r1", 0x0008},
        {"triangle", 0x0010}, {"circle", 0x0020}, {"cross", 0x0040}, {"square", 0x0080},
        {"select", 0x0100}, {"start", 0x0800},
        {"up", 0x1000}, {"right", 0x2000}, {"down", 0x4000}, {"left", 0x8000}};
    uint32_t bits = 0;
    char buf[96]; snprintf(buf, sizeof buf, "%s", list);
    for (char *t = strtok(buf, "+"); t; t = strtok(NULL, "+")) {
        int found = 0;
        for (size_t i = 0; i < sizeof names / sizeof names[0]; ++i)
            if (!strcmp(t, names[i].n)) { bits |= names[i].b; found = 1; }
        if (!found) logf_line("unknown button '%s'%.0ld", t, 0);
    }
    return bits;
}

static void finish(int failed) {
    if (st_state == ST_QUIT) return;
    if (failed) st_failed = 1;
    if (st_log) {
        fprintf(st_log, "capture %s: %d pictures\n", st_failed ? "STOPPED" : "DONE", st_shots);
        fflush(st_log);
    }
    if (!st_failed) {
        char dp[460]; snprintf(dp, sizeof dp, "%s/capture-ok.txt", st_out);
        FILE *d = fopen(dp, "w");
        if (d) { fprintf(d, "%d\n", st_shots); fclose(d); }
    }
    st_state = ST_QUIT;
    st_left = 300;   /* frames to close normally before a hard exit */
#if defined(PSX_SDL3)
    SDL_Event e; SDL_zero(e); e.type = SDL_EVENT_QUIT;
    SDL_PushEvent(&e);
#endif
}

/* Reads script lines until one starts a step. */
static void next_step(void) {
    char line[256];
    while (st_script && fgets(line, sizeof line, st_script)) {
        ++st_line_no;
        char *h = strchr(line, '#'); if (h) *h = 0;
        char cmd[16] = {0}, a[96] = {0};
        long b = 0, c = 0;
        int n = sscanf(line, "%15s %95s %ld %ld", cmd, a, &b, &c);
        if (n < 1) continue;
        st_phase = 0;
        if (!strcmp(cmd, "wait")) {
            st_state = ST_WAIT; st_left = n >= 2 ? atoi(a) : 1; return;
        } else if (!strcmp(cmd, "press") && n >= 2) {
            st_state = ST_PRESS; st_buttons = button_bits(a); st_hold = n >= 3 ? (int)b : 3;
            st_left = st_hold; st_mouse = 0; return;
        } else if ((!strcmp(cmd, "point") || !strcmp(cmd, "click")) && n >= 3) {
            st_x = atoi(a); st_y = (int)b; st_mouse = 1;
            if (st_x < 16) st_x = 16; if (st_x > 495) st_x = 495;
            if (st_y < 8) st_y = 8; if (st_y > 231) st_y = 231;
            st_state = cmd[0] == 'p' ? ST_POINT : ST_CLICK; st_left = 4; return;
        } else if (!strcmp(cmd, "screen") && n >= 2) {
            st_state = ST_SCREEN; st_target_ra = (uint32_t)strtoul(a, NULL, 16);
            st_left = n >= 3 ? (int)b : 1800; return;
        } else if (!strcmp(cmd, "shot") && n >= 2) {
            snprintf(st_name, sizeof st_name, "%s", a);
            char p[520]; snprintf(p, sizeof p, "%s/%s.png", st_out, st_name);
            st_shot_seq = present_shot_seq();
            if (!present_shot_request(p)) { logf_line("shot %s: no picture possible (renderer)%.0ld", st_name, 0); finish(1); return; }
            gl_renderer_invalidate_present();
            st_state = ST_SHOT; st_left = 180; return;
        } else if (!strcmp(cmd, "quit")) {
            finish(0); return;
        } else {
            logf_line("unknown line: %s%.0ld", cmd, 0);
        }
    }
    finish(0);
}

int ch_capture_frame(CPUState *cpu, uint32_t screen_ra) {
    if (!ch_capture_enabled()) return 0;
    ++st_frames;
    uint32_t pad = 0;
    for (int guard = 0; guard < 64; ++guard) {
        if (st_state == ST_IDLE) { next_step(); continue; }
        if (st_state == ST_QUIT) {
            if (--st_left <= 0) { if (st_log) fclose(st_log); st_log = NULL; ch_exit_now(st_failed ? 3 : 0); }
            break;
        }
        if (st_state == ST_WAIT) {
            if (st_left-- > 0) break;
            st_state = ST_IDLE; continue;
        }
        if (st_state == ST_PRESS || st_state == ST_CLICK || st_state == ST_POINT) {
            /* phase 0: cursor rests (click/point); 1: buttons held; 2: released */
            if (st_phase == 0 && st_state == ST_PRESS) st_phase = 1;
            if (st_phase == 0) {
                if (st_left-- > 0) break;
                if (st_state == ST_POINT) { st_state = ST_IDLE; continue; }
                st_phase = 1; st_buttons = 0x0040; st_left = 3;
            }
            if (st_phase == 1) {
                if (st_left-- > 0) { pad = st_buttons; break; }
                st_phase = 2; st_left = 10;
            }
            if (st_left-- > 0) break;
            st_state = ST_IDLE; continue;
        }
        if (st_state == ST_SCREEN) {
            if (screen_ra == st_target_ra) { logf_line("screen %s reached%.0ld", "", 0); st_state = ST_IDLE; continue; }
            if (st_left-- <= 0) {
                char ra[16]; snprintf(ra, sizeof ra, "%08X", (unsigned)st_target_ra);
                logf_line("screen %s not reached (on screen %ld)", ra, (long)screen_ra);
                finish(1);
            }
            break;
        }
        if (st_state == ST_SHOT) {
            if (present_shot_seq() != st_shot_seq) {
                if (present_shot_ok()) { ++st_shots; logf_line("shot %s saved%.0ld", st_name, 0); }
                else logf_line("shot %s: could not be written%.0ld", st_name, 0);
                st_state = ST_IDLE; continue;
            }
            if (st_left % 30 == 0) gl_renderer_invalidate_present();
            if (st_left-- <= 0) { logf_line("shot %s: no frame arrived%.0ld", st_name, 0); finish(1); }
            break;
        }
        break;
    }
    /* The menus stay in pad mode (as opened); point / click just move the
     * cursor, and the hotspot navigator then picks the box under it, as when
     * the D-pad lands there. The game's arrow is hidden while capturing
     * (func_8003E4C8 in mouse_pad_hle.c), so the pictures show only the
     * menu's own highlight. */
    cpu->write_word(PAD_BUTTONS, pad);
    cpu->write_word(MOUSE_MODE, 0u);
    cpu->write_word(cpu->gpr[28] + 0xC10u, 0u);
    if (st_mouse) {
        cpu->write_word(CURSOR_X, (uint32_t)st_x);
        cpu->write_word(CURSOR_Y, (uint32_t)st_y);
        st_mouse = 0;   /* written once; the navigator takes it from there */
    }
    return 1;
}
