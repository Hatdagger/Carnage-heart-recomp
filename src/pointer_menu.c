/* Carnage Heart: window pointer, touch and right-click button menu.
 *
 * The game has a mouse cursor (restored from Carnage Heart EZ in
 * mouse_pad_hle.c), but a PS1 Mouse on port 1 only gives two buttons and
 * replaces the gamepad. This module instead reads the host pointer over the
 * game window and hands the game a cursor position plus button bits that
 * mouse_pad_hle.c ORs into the normal pad word:
 *
 *   Left click            Cross (confirm) on the item under the pointer
 *   Right click           opens a small labelled menu at the pointer with the
 *                         buttons a mouse lacks. On a known menu screen it
 *                         lists only the buttons that do something there, with
 *                         what they do ("SELECT: QUIT GAME"), plus MORE BUTTONS
 *                         for the full list; elsewhere it lists Triangle,
 *                         Square, Start, Select, L1, R1, L2, R2. Back is always
 *                         last. Clicking an entry presses that button on the
 *                         item you right-clicked. Clicking anywhere else closes
 *                         the menu.
 *   Double right click    Circle (back). The menu opens on the first click and
 *                         closes again on the second, so there is no delay.
 *   Mouse wheel           on the chip program grid: scroll the grid (R1 held
 *                         with the D-pad, as on the pad); Shift + wheel or a
 *                         sideways wheel / touchpad swipe scrolls left-right.
 *                         On list screens with up/down arrow boxes (card
 *                         list and others, see mouse_pad_hle.c): one notch
 *                         presses the one-row arrow (the hotspot routine
 *                         reports that box for the press, so the pointer
 *                         does not move)
 *   Touch tap             move + Cross
 *   Touch drag            move only
 *   Touch long press      the same menu
 *   Two-finger tap        Circle (back)
 *
 * Input comes from an SDL event watch, which observes events without taking
 * them from the runtime or the in-game menu, and catches very short clicks
 * (touchpad taps) that polling would miss. The menu is drawn with the
 * runtime's HUD hook (OpenGL), on top of the game image.
 *
 * Off during netplay (this is local-only input and would desync the peers),
 * while the in-game menu owns input, and while the runtime's PS1 Mouse mode
 * has the pointer captured. CH_POINTER=0 turns it off entirely. */
#include "pointer_menu.h"
#include "screen_labels.h"

#include <stdlib.h>
#include <string.h>

#if defined(PSX_SDL3)
#include <SDL3/SDL.h>
#include <SDL3/SDL_opengl.h>
#include "host_overlay.h"
#include "psx_netplay.h"

/* ---- Game button bits (game order, see Mouse Support Findings) ---------- */
#define B_TRIANGLE 0x0010u
#define B_CIRCLE   0x0020u
#define B_CROSS    0x0040u
#define B_SQUARE   0x0080u
#define B_L2       0x0001u
#define B_R2       0x0002u
#define B_L1       0x0004u
#define B_R1       0x0008u
#define B_SELECT   0x0100u
#define B_START    0x0800u
#define B_UP       0x1000u
#define B_RIGHT    0x2000u
#define B_DOWN     0x4000u
#define B_LEFT     0x8000u

/* Chip program grid screen (0x80041188, see src/screen_labels.c). Its helper
 * 0x80047948 scrolls the grid while R1 is held with the D-pad: 2 px a frame
 * up/down (rows are 40 px), 4 px a frame left/right (columns are 56 px). */
#define CHIP_GRID_START 0x80041188u
#define CHIP_GRID_END   0x800415DCu
#define WHEEL_FRAMES_Y  10              /* one wheel notch = half a row */
#define WHEEL_FRAMES_X  7               /* one notch = half a column */
#define WHEEL_MAX       60
/* List screens: one notch = Cross held LIST_PRESS frames on the one-row
 * arrow box, then LIST_GAP frames before the next notch (a one-row scroll
 * takes the card list 8 frames; a press during it is ignored by the game). */
#define LIST_PRESS      2
#define LIST_GAP        10
#define LIST_MAX_NOTCHES 6

/* kind: a button, or MORE (show every button), or BACK (Circle). */
enum { K_BUTTON, K_MORE, K_BACK };
typedef struct { const char *label; uint32_t bit; int kind; } MenuItem;
static const MenuItem k_all[] = {
    { "TRIANGLE", B_TRIANGLE }, { "SQUARE", B_SQUARE },
    { "START",    B_START    }, { "SELECT", B_SELECT },
    { "L1",       B_L1       }, { "R1",     B_R1     },
    { "L2",       B_L2       }, { "R2",     B_R2     },
};
#define N_ALL ((int)(sizeof k_all / sizeof k_all[0]))
static const MenuItem k_more = { "MORE BUTTONS", 0, K_MORE };
static const MenuItem k_back = { "BACK", B_CIRCLE, K_BACK };

/* Menu layout in menu pixels (multiplied by the draw scale). The entries are
 * chosen when the menu opens, so its size varies; the image buffer is sized
 * for the largest menu. */
#define FONT_W   8
#define ROW_H    11
#define PAD      3
#define MAX_ITEMS 12
#define MAX_COLS  20
#define MAX_W    (PAD * 2 + MAX_COLS * FONT_W)
#define MAX_H    (PAD * 2 + MAX_ITEMS * ROW_H)

#define DOUBLE_CLICK_MS 350u
#define LONG_PRESS_MS   500u
#define TAP_SLOP_PX     14.0f
#define PRESS_FRAMES    3               /* how long a fired button is held */

/* Public-domain 8x8 ASCII 32..90 subset from font8x8_basic
 * (https://github.com/d7samurai/font8x8), as in the runtime's menus. */
static const uint8_t FONT8[59][8] = {
    {0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00}, {0x18,0x3C,0x3C,0x18,0x18,0x00,0x18,0x00},
    {0x36,0x36,0x00,0x00,0x00,0x00,0x00,0x00}, {0x36,0x36,0x7F,0x36,0x7F,0x36,0x36,0x00},
    {0x0C,0x3E,0x03,0x1E,0x30,0x1F,0x0C,0x00}, {0x00,0x63,0x33,0x18,0x0C,0x66,0x63,0x00},
    {0x1C,0x36,0x1C,0x6E,0x3B,0x33,0x6E,0x00}, {0x06,0x06,0x03,0x00,0x00,0x00,0x00,0x00},
    {0x18,0x0C,0x06,0x06,0x06,0x0C,0x18,0x00}, {0x06,0x0C,0x18,0x18,0x18,0x0C,0x06,0x00},
    {0x00,0x66,0x3C,0xFF,0x3C,0x66,0x00,0x00}, {0x00,0x0C,0x0C,0x3F,0x0C,0x0C,0x00,0x00},
    {0x00,0x00,0x00,0x00,0x00,0x0C,0x0C,0x06}, {0x00,0x00,0x00,0x3F,0x00,0x00,0x00,0x00},
    {0x00,0x00,0x00,0x00,0x00,0x0C,0x0C,0x00}, {0x60,0x30,0x18,0x0C,0x06,0x03,0x01,0x00},
    {0x3E,0x63,0x73,0x7B,0x6F,0x67,0x3E,0x00}, {0x0C,0x0E,0x0C,0x0C,0x0C,0x0C,0x3F,0x00},
    {0x1E,0x33,0x30,0x1C,0x06,0x33,0x3F,0x00}, {0x1E,0x33,0x30,0x1C,0x30,0x33,0x1E,0x00},
    {0x38,0x3C,0x36,0x33,0x7F,0x30,0x78,0x00}, {0x3F,0x03,0x1F,0x30,0x30,0x33,0x1E,0x00},
    {0x1C,0x06,0x03,0x1F,0x33,0x33,0x1E,0x00}, {0x3F,0x33,0x30,0x18,0x0C,0x0C,0x0C,0x00},
    {0x1E,0x33,0x33,0x1E,0x33,0x33,0x1E,0x00}, {0x1E,0x33,0x33,0x3E,0x30,0x18,0x0E,0x00},
    {0x00,0x0C,0x0C,0x00,0x00,0x0C,0x0C,0x00}, {0x00,0x0C,0x0C,0x00,0x00,0x0C,0x0C,0x06},
    {0x18,0x0C,0x06,0x03,0x06,0x0C,0x18,0x00}, {0x00,0x00,0x3F,0x00,0x00,0x3F,0x00,0x00},
    {0x06,0x0C,0x18,0x30,0x18,0x0C,0x06,0x00}, {0x1E,0x33,0x30,0x18,0x0C,0x00,0x0C,0x00},
    {0x3E,0x63,0x7B,0x7B,0x7B,0x03,0x1E,0x00}, {0x0C,0x1E,0x33,0x33,0x3F,0x33,0x33,0x00},
    {0x3F,0x66,0x66,0x3E,0x66,0x66,0x3F,0x00}, {0x3C,0x66,0x03,0x03,0x03,0x66,0x3C,0x00},
    {0x1F,0x36,0x66,0x66,0x66,0x36,0x1F,0x00}, {0x7F,0x06,0x06,0x3E,0x06,0x06,0x7F,0x00},
    {0x7F,0x06,0x06,0x3E,0x06,0x06,0x06,0x00}, {0x3C,0x66,0x03,0x03,0x73,0x66,0x7C,0x00},
    {0x33,0x33,0x33,0x3F,0x33,0x33,0x33,0x00}, {0x1E,0x0C,0x0C,0x0C,0x0C,0x0C,0x1E,0x00},
    {0x78,0x30,0x30,0x30,0x33,0x33,0x1E,0x00}, {0x67,0x66,0x36,0x1E,0x36,0x66,0x67,0x00},
    {0x06,0x06,0x06,0x06,0x06,0x06,0x7F,0x00}, {0x63,0x77,0x7F,0x7F,0x6B,0x63,0x63,0x00},
    {0x63,0x67,0x6F,0x7B,0x73,0x63,0x63,0x00}, {0x1C,0x36,0x63,0x63,0x63,0x36,0x1C,0x00},
    {0x3F,0x66,0x66,0x3E,0x06,0x06,0x06,0x00}, {0x1E,0x33,0x33,0x33,0x3B,0x1E,0x38,0x00},
    {0x3F,0x66,0x66,0x3E,0x36,0x66,0x67,0x00}, {0x1E,0x33,0x07,0x0E,0x38,0x33,0x1E,0x00},
    {0x3F,0x2D,0x0C,0x0C,0x0C,0x0C,0x1E,0x00}, {0x33,0x33,0x33,0x33,0x33,0x33,0x3F,0x00},
    {0x33,0x33,0x33,0x33,0x33,0x1E,0x0C,0x00}, {0x63,0x63,0x63,0x6B,0x7F,0x77,0x63,0x00},
    {0x63,0x63,0x36,0x1C,0x1C,0x36,0x63,0x00}, {0x33,0x33,0x33,0x1E,0x0C,0x0C,0x1E,0x00},
    {0x7F,0x63,0x31,0x18,0x4C,0x66,0x7F,0x00},
};

/* ---- Shared state (event watch and frame code on the main thread, the
 * draw callback possibly on the render thread) ---------------------------- */
static SDL_Mutex *s_mu;
static int        s_watch_on, s_hud_on;
static SDL_WindowID s_win;
static float      s_px, s_py;           /* pointer, window coordinates */
static int        s_activity;
static int        s_left_held, s_left_latch;
static uint32_t   s_fire_bits; static int s_fire_frames;
static int        s_menu_open;
static float      s_menu_x, s_menu_y;   /* window coords of the menu's corner */
static float      s_anchor_x, s_anchor_y; /* pointer where it was opened */
static Uint64     s_last_right_ms;
static int        s_menu_scale = 1;     /* set by the draw callback */
static float      s_pixel_density = 1.0f;
static SDL_AtomicInt s_draws;
static int        s_ww, s_wh;           /* window size, from the main thread */
static int        s_last_draws, s_frames_since_draw;
static uint32_t   s_screen_ra;          /* menu screen on show, 0 = none */
static MenuItem   s_items[MAX_ITEMS];   /* entries of the open menu */
static int        s_n_items, s_mw = MAX_W, s_mh = MAX_H; /* menu size, menu px */
static float      s_wheel_ax, s_wheel_ay; /* wheel movement not yet a notch */
static int        s_wheel_x, s_wheel_y; /* scroll frames to play, signed */
static int        s_wheel_n;            /* whole vertical notches, + = up */
static int        s_list_on;            /* a list with arrow boxes is on show */
static int        s_list_dir, s_list_step; /* -1 up / +1 down; step = this frame's */
static int        s_list_hold, s_list_gap;

/* Touch */
static SDL_FingerID s_f1; static int s_fingers, s_f1_down, s_f_drag, s_f_two, s_f_consumed;
static float      s_f_x0, s_f_y0; static Uint64 s_f_t0;

static void lock(void)   { if (s_mu) SDL_LockMutex(s_mu); }
static void unlock(void) { if (s_mu) SDL_UnlockMutex(s_mu); }

static int win_size(int *w, int *h) {
    SDL_Window *win = s_win ? SDL_GetWindowFromID(s_win) : NULL;
    if (!win) { int n = 0; SDL_Window **all = SDL_GetWindows(&n); if (all && n > 0) { win = all[0]; s_win = SDL_GetWindowID(win); } SDL_free(all); }
    if (!win || !SDL_GetWindowSize(win, w, h) || *w <= 0 || *h <= 0) return 0;
    return 1;
}

/* The runtime letterboxes the 4:3 game image in the window. */
static void game_rect(int ww, int wh, float *rx, float *ry, float *rw, float *rh) {
    float dw = (float)ww, dh = (float)ww * 3.0f / 4.0f;
    if (dh > (float)wh) { dh = (float)wh; dw = (float)wh * 4.0f / 3.0f; }
    *rx = ((float)ww - dw) * 0.5f; *ry = ((float)wh - dh) * 0.5f; *rw = dw; *rh = dh;
}

/* Menu size in window coordinates. */
static void menu_size(float *w, float *h) {
    float s = (float)s_menu_scale / (s_pixel_density > 0.0f ? s_pixel_density : 1.0f);
    *w = (float)s_mw * s; *h = (float)s_mh * s;
}

static int menu_item_at(float x, float y) {
    float w, h; menu_size(&w, &h);
    if (x < s_menu_x || y < s_menu_y || x >= s_menu_x + w || y >= s_menu_y + h) return -1;
    float row = (y - s_menu_y) / (h / (float)s_mh);    /* in menu pixels */
    int i = (int)((row - PAD) / ROW_H);
    return (i >= 0 && i < s_n_items) ? i : -2;         /* -2: on the frame */
}

/* Choose the entries: the screen's own buttons with what they do, MORE
 * BUTTONS and BACK on a known menu screen; every button and BACK elsewhere,
 * or when all is set (MORE BUTTONS was clicked). */
static void menu_fill(int all) {
    int n = 0;
    ChScreenItem sc[8];
    const int k = (!all && s_screen_ra) ? ch_screen_items(s_screen_ra, sc, 8) : -1;
    if (k >= 0) {
        for (int i = 0; i < k; ++i) s_items[n++] = (MenuItem){ sc[i].label, sc[i].bit, K_BUTTON };
        s_items[n++] = k_more;
    } else {
        for (int i = 0; i < N_ALL; ++i) s_items[n++] = k_all[i];
    }
    s_items[n++] = k_back;
    s_n_items = n;
    int cols = 0;
    for (int i = 0; i < n; ++i) { int l = (int)strlen(s_items[i].label); if (l > cols) cols = l; }
    if (cols > MAX_COLS - 1) cols = MAX_COLS - 1;
    s_mw = PAD * 2 + (cols + 1) * FONT_W;
    s_mh = PAD * 2 + n * ROW_H;
}

static void menu_place(float x, float y) {
    int ww = 0, wh = 0; float w, h;
    menu_size(&w, &h);
    float mx = x + 2.0f, my = y + 2.0f;
    if (win_size(&ww, &wh)) {
        float rx, ry, rw, rh; game_rect(ww, wh, &rx, &ry, &rw, &rh);
        if (mx + w > rx + rw) mx = x - w - 2.0f;       /* flip left at the edge */
        if (my + h > ry + rh) my = ry + rh - h;        /* push up at the bottom */
        if (mx < rx) mx = rx;
        if (my < ry) my = ry;
    }
    s_menu_x = mx; s_menu_y = my; s_menu_open = 1;
}

static void menu_open_at(float x, float y) {
    s_anchor_x = x; s_anchor_y = y;
    menu_fill(0);
    menu_place(x, y);
}

static void fire(uint32_t bit) { s_fire_bits |= bit; s_fire_frames = PRESS_FRAMES; s_activity = 1; }

/* A primary press (left click or touch tap) at x, y. */
static void primary_press(float x, float y) {
    if (s_menu_open) {
        int i = menu_item_at(x, y);
        if (i >= 0 && s_items[i].kind == K_MORE) {     /* expand in place */
            menu_fill(1); menu_place(s_anchor_x, s_anchor_y); s_activity = 1;
            return;
        }
        if (i >= 0) fire(s_items[i].bit);
        if (i != -2) s_menu_open = 0;                  /* entry or outside */
        return;
    }
    s_left_held = 1; s_left_latch = 1; s_activity = 1;
}

static void secondary_press(float x, float y, Uint64 ms) {
    const int dbl = s_menu_open && ms - s_last_right_ms <= DOUBLE_CLICK_MS;
    s_last_right_ms = ms;
    if (dbl) { s_menu_open = 0; fire(B_CIRCLE); s_last_right_ms = 0; return; }
    s_left_held = 0;
    menu_open_at(x, y);
    s_activity = 1;
}

static int ignore_input(void) {
    if (psx_host_ui_capture_active()) return 1;
    SDL_Window *win = s_win ? SDL_GetWindowFromID(s_win) : NULL;
    return win && SDL_GetWindowRelativeMouseMode(win);
}

static bool SDLCALL watch(void *ud, SDL_Event *ev) {
    (void)ud;
    lock();
    const Uint64 ms = ev->common.timestamp / 1000000u;
    switch (ev->type) {
    case SDL_EVENT_MOUSE_MOTION:
        if (ev->motion.which == SDL_TOUCH_MOUSEID || ignore_input()) break;
        s_win = ev->motion.windowID; s_px = ev->motion.x; s_py = ev->motion.y; s_activity = 1;
        break;
    case SDL_EVENT_MOUSE_BUTTON_DOWN:
    case SDL_EVENT_MOUSE_BUTTON_UP: {
        if (ev->button.which == SDL_TOUCH_MOUSEID || ignore_input()) break;
        const int down = ev->type == SDL_EVENT_MOUSE_BUTTON_DOWN;
        s_win = ev->button.windowID; s_px = ev->button.x; s_py = ev->button.y;
        if (ev->button.button == SDL_BUTTON_LEFT) {
            if (down) primary_press(s_px, s_py); else s_left_held = 0;
        } else if (ev->button.button == SDL_BUTTON_RIGHT && down) {
            secondary_press(s_px, s_py, ms);
        }
        break;
    }
    case SDL_EVENT_MOUSE_WHEEL: {
        /* The wheel direction follows the system setting (natural scrolling
         * reaches us already flipped, so it stays as the player set it). */
        if (ev->wheel.which == SDL_TOUCH_MOUSEID || ignore_input()) break;
        float dx = ev->wheel.x, dy = ev->wheel.y;
        if ((SDL_GetModState() & SDL_KMOD_SHIFT) && dx == 0.0f) { dx = -dy; dy = 0.0f; }
        s_win = ev->wheel.windowID;
        s_wheel_ax += dx; s_wheel_ay += dy;
        /* Whole notches only; touchpads send fractions that add up. */
        while (s_wheel_ay >= 1.0f)  { s_wheel_ay -= 1.0f; s_wheel_y += WHEEL_FRAMES_Y; ++s_wheel_n; }
        while (s_wheel_ay <= -1.0f) { s_wheel_ay += 1.0f; s_wheel_y -= WHEEL_FRAMES_Y; --s_wheel_n; }
        if (s_wheel_n > LIST_MAX_NOTCHES) s_wheel_n = LIST_MAX_NOTCHES;
        else if (s_wheel_n < -LIST_MAX_NOTCHES) s_wheel_n = -LIST_MAX_NOTCHES;
        while (s_wheel_ax >= 1.0f)  { s_wheel_ax -= 1.0f; s_wheel_x += WHEEL_FRAMES_X; }
        while (s_wheel_ax <= -1.0f) { s_wheel_ax += 1.0f; s_wheel_x -= WHEEL_FRAMES_X; }
        if (s_wheel_y > WHEEL_MAX) s_wheel_y = WHEEL_MAX; else if (s_wheel_y < -WHEEL_MAX) s_wheel_y = -WHEEL_MAX;
        if (s_wheel_x > WHEEL_MAX) s_wheel_x = WHEEL_MAX; else if (s_wheel_x < -WHEEL_MAX) s_wheel_x = -WHEEL_MAX;
        break;
    }
    case SDL_EVENT_FINGER_DOWN:
    case SDL_EVENT_FINGER_MOTION:
    case SDL_EVENT_FINGER_UP: {
        if (ignore_input()) break;
        int ww = 0, wh = 0;
        if (ev->tfinger.windowID) s_win = ev->tfinger.windowID;
        if (!win_size(&ww, &wh)) break;
        const float x = ev->tfinger.x * (float)ww, y = ev->tfinger.y * (float)wh;
        if (ev->type == SDL_EVENT_FINGER_DOWN) {
            if (++s_fingers == 1) {
                s_f1 = ev->tfinger.fingerID; s_f1_down = 1; s_f_drag = 0; s_f_two = 0; s_f_consumed = 0;
                s_f_x0 = x; s_f_y0 = y; s_f_t0 = ms;
                s_px = x; s_py = y; s_activity = 1;
            } else if (s_f1_down && !s_f_drag && ms - s_f_t0 < 300u) {
                s_f_two = 1;                           /* two-finger tap */
            }
        } else if (ev->type == SDL_EVENT_FINGER_MOTION) {
            if (ev->tfinger.fingerID != s_f1 || !s_f1_down) break;
            if (SDL_fabsf(x - s_f_x0) > TAP_SLOP_PX || SDL_fabsf(y - s_f_y0) > TAP_SLOP_PX) s_f_drag = 1;
            if (!s_menu_open) { s_px = x; s_py = y; s_activity = 1; }
        } else {
            if (s_fingers > 0) --s_fingers;
            if (ev->tfinger.fingerID == s_f1 && s_f1_down) {
                s_f1_down = 0;
                if (s_f_two) { s_menu_open = 0; fire(B_CIRCLE); }
                else if (!s_f_consumed && !s_f_drag && !s_menu_open && ms - s_f_t0 >= LONG_PRESS_MS) {
                    menu_open_at(x, y); s_activity = 1;        /* long press, lifted */
                }
                else if (!s_f_consumed && (!s_f_drag || s_menu_open)) {
                    if (!s_menu_open) { s_px = x; s_py = y; }
                    primary_press(x, y);
                    s_left_held = 0;                   /* the latch still gives a press */
                }
            }
        }
        break;
    }
    default: break;
    }
    unlock();
    return true;
}

/* ---- Drawing (HUD hook, GL 3.3 core) ------------------------------------ */
typedef struct {
    GLuint prog, vao, vbo, tex; int ok, tried;
} Gl;
static Gl g;

#define GLF(ret, name, args) static ret (APIENTRY *p_##name) args
GLF(GLuint, glCreateShader, (GLenum)); GLF(void, glShaderSource, (GLuint, GLsizei, const GLchar *const *, const GLint *));
GLF(void, glCompileShader, (GLuint)); GLF(GLuint, glCreateProgram, (void)); GLF(void, glAttachShader, (GLuint, GLuint));
GLF(void, glLinkProgram, (GLuint)); GLF(void, glGetProgramiv, (GLuint, GLenum, GLint *)); GLF(void, glUseProgram, (GLuint));
GLF(void, glDeleteShader, (GLuint)); GLF(void, glGenVertexArrays, (GLsizei, GLuint *)); GLF(void, glBindVertexArray, (GLuint));
GLF(void, glGenBuffers, (GLsizei, GLuint *)); GLF(void, glBindBuffer, (GLenum, GLuint));
GLF(void, glBufferData, (GLenum, GLsizeiptr, const void *, GLenum)); GLF(void, glVertexAttribPointer, (GLuint, GLint, GLenum, GLboolean, GLsizei, const void *));
GLF(void, glEnableVertexAttribArray, (GLuint)); GLF(void, glActiveTexture, (GLenum)); GLF(void, glBindSampler, (GLuint, GLuint));
GLF(GLint, glGetUniformLocation, (GLuint, const GLchar *)); GLF(void, glUniform1i, (GLint, GLint));
GLF(void, glBlendFuncSeparate, (GLenum, GLenum, GLenum, GLenum)); GLF(void, glBlendEquationSeparate, (GLenum, GLenum));
GLF(void, glGenTextures, (GLsizei, GLuint *)); GLF(void, glBindTexture, (GLenum, GLuint));
GLF(void, glTexImage2D, (GLenum, GLint, GLint, GLsizei, GLsizei, GLint, GLenum, GLenum, const void *));
GLF(void, glTexParameteri, (GLenum, GLenum, GLint)); GLF(void, glDrawArrays, (GLenum, GLint, GLsizei));
GLF(void, glGetIntegerv, (GLenum, GLint *)); GLF(GLboolean, glIsEnabled, (GLenum));
GLF(void, glEnable, (GLenum)); GLF(void, glDisable, (GLenum)); GLF(void, glViewport, (GLint, GLint, GLsizei, GLsizei));
GLF(void, glPixelStorei, (GLenum, GLint));

#ifndef GL_SAMPLER_BINDING
#define GL_SAMPLER_BINDING 0x8919
#endif
#ifndef GL_VERTEX_ARRAY_BINDING
#define GL_VERTEX_ARRAY_BINDING 0x85B5
#endif

static int gl_init(void) {
    if (g.tried) return g.ok;
    g.tried = 1;
    int ok = 1;
#define L(n) do { *(void **)&p_##n = (void *)SDL_GL_GetProcAddress(#n); if (!p_##n) ok = 0; } while (0)
    L(glCreateShader); L(glShaderSource); L(glCompileShader); L(glCreateProgram); L(glAttachShader);
    L(glLinkProgram); L(glGetProgramiv); L(glUseProgram); L(glDeleteShader); L(glGenVertexArrays);
    L(glBindVertexArray); L(glGenBuffers); L(glBindBuffer); L(glBufferData); L(glVertexAttribPointer);
    L(glEnableVertexAttribArray); L(glActiveTexture); L(glBindSampler); L(glGetUniformLocation);
    L(glUniform1i); L(glBlendFuncSeparate); L(glBlendEquationSeparate); L(glGenTextures); L(glBindTexture);
    L(glTexImage2D); L(glTexParameteri); L(glDrawArrays); L(glGetIntegerv); L(glIsEnabled);
    L(glEnable); L(glDisable); L(glViewport); L(glPixelStorei);
#undef L
    if (!ok) return 0;
    static const char *vs =
        "#version 330\nlayout(location=0) in vec2 p; layout(location=1) in vec2 t; out vec2 uv;\n"
        "void main(){ uv=t; gl_Position=vec4(p,0.0,1.0); }\n";
    static const char *fs =
        "#version 330\nin vec2 uv; uniform sampler2D img; out vec4 c;\n"
        "void main(){ c=texture(img,uv); }\n";
    GLuint v = p_glCreateShader(GL_VERTEX_SHADER), f = p_glCreateShader(GL_FRAGMENT_SHADER);
    p_glShaderSource(v, 1, &vs, NULL); p_glCompileShader(v);
    p_glShaderSource(f, 1, &fs, NULL); p_glCompileShader(f);
    g.prog = p_glCreateProgram(); p_glAttachShader(g.prog, v); p_glAttachShader(g.prog, f);
    p_glLinkProgram(g.prog); p_glDeleteShader(v); p_glDeleteShader(f);
    GLint linked = 0; p_glGetProgramiv(g.prog, GL_LINK_STATUS, &linked);
    if (!linked) return 0;
    p_glGenVertexArrays(1, &g.vao); p_glGenBuffers(1, &g.vbo); p_glGenTextures(1, &g.tex);
    p_glBindVertexArray(g.vao); p_glBindBuffer(GL_ARRAY_BUFFER, g.vbo);
    p_glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, 16, (void *)0);
    p_glVertexAttribPointer(1, 2, GL_FLOAT, GL_FALSE, 16, (void *)8);
    p_glEnableVertexAttribArray(0); p_glEnableVertexAttribArray(1);
    p_glBindTexture(GL_TEXTURE_2D, g.tex);
    p_glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    p_glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    p_glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    p_glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    g.ok = 1;
    return 1;
}

/* RGBA image of the menu, hover row highlighted. Colours follow the game's
 * dark-blue panels. */
static uint32_t s_img[MAX_W * MAX_H];
static int s_iw, s_ih;                  /* size of the image being drawn */
static void put(int x, int y, uint32_t c) { if (x >= 0 && y >= 0 && x < s_iw && y < s_ih) s_img[y * s_iw + x] = c; }
#define RGBA(r, g_, b, a) ((uint32_t)(r) | ((uint32_t)(g_) << 8) | ((uint32_t)(b) << 16) | ((uint32_t)(a) << 24))
static void render_menu(const MenuItem *items, int n, int w, int h, int hover) {
    const uint32_t bg = RGBA(16, 24, 56, 230), edge = RGBA(150, 170, 230, 255),
                   hi = RGBA(60, 90, 180, 255), fg = RGBA(235, 235, 245, 255), dim = RGBA(150, 160, 190, 255);
    s_iw = w; s_ih = h;
    for (int i = 0; i < w * h; ++i) s_img[i] = bg;
    for (int x = 0; x < w; ++x) { put(x, 0, edge); put(x, h - 1, edge); }
    for (int y = 0; y < h; ++y) { put(0, y, edge); put(w - 1, y, edge); }
    for (int i = 0; i < n; ++i) {
        const int ry = PAD + i * ROW_H;
        if (i == hover)
            for (int y = ry; y < ry + ROW_H; ++y) for (int x = 1; x < w - 1; ++x) put(x, y, hi);
        if (i > 0 && items[i].kind != K_BUTTON && items[i - 1].kind == K_BUTTON)
            for (int x = PAD; x < w - PAD; ++x) put(x, ry - 1, dim);   /* separator */
        const uint32_t col = items[i].kind == K_MORE ? dim : fg;
        const char *s = items[i].label;
        for (int c = 0; s[c] && c < MAX_COLS - 1; ++c) {
            int ch = (unsigned char)s[c]; if (ch < 32 || ch > 90) ch = '?';
            const uint8_t *gl = FONT8[ch - 32];
            for (int gy = 0; gy < 8; ++gy) for (int gx = 0; gx < 8; ++gx)
                if (gl[gy] & (1u << gx)) put(PAD + 2 + c * FONT_W + gx, ry + 2 + gy, col);
        }
    }
}

static void SDLCALL hud_draw(int w, int h, void *ctx) {
    (void)ctx;
    SDL_AddAtomicInt(&s_draws, 1);
    int open, ww, wh; float mx, my, px, py;
    lock();
    open = s_menu_open; mx = s_menu_x; my = s_menu_y; px = s_px; py = s_py;
    ww = s_ww; wh = s_wh;
    int have = ww > 0 && wh > 0;
    if (have) {
        s_pixel_density = (float)w / (float)ww;
        float rx, ry, rw, rh; game_rect(w, h, &rx, &ry, &rw, &rh);
        int sc = (int)(rh / 450.0f); s_menu_scale = sc < 1 ? 1 : sc;
    }
    int hover = open ? menu_item_at(px, py) : -1;
    MenuItem items[MAX_ITEMS]; const int n = s_n_items, mw = s_mw, mh = s_mh;
    memcpy(items, s_items, sizeof items);
    unlock();
    if (!open || !have || !gl_init()) return;
    render_menu(items, n, mw, mh, hover);

    GLint prog, vao, abuf, atex, tex0, samp, vp[4], bsrc, bdst, bsrca, bdsta, beq, beqa, unpack, rowlen;
    p_glGetIntegerv(GL_CURRENT_PROGRAM, &prog); p_glGetIntegerv(GL_VERTEX_ARRAY_BINDING, &vao);
    p_glGetIntegerv(GL_ARRAY_BUFFER_BINDING, &abuf); p_glGetIntegerv(GL_ACTIVE_TEXTURE, &atex);
    p_glActiveTexture(GL_TEXTURE0);
    p_glGetIntegerv(GL_TEXTURE_BINDING_2D, &tex0); p_glGetIntegerv(GL_SAMPLER_BINDING, &samp);
    p_glGetIntegerv(GL_VIEWPORT, vp);
    p_glGetIntegerv(GL_BLEND_SRC_RGB, &bsrc); p_glGetIntegerv(GL_BLEND_DST_RGB, &bdst);
    p_glGetIntegerv(GL_BLEND_SRC_ALPHA, &bsrca); p_glGetIntegerv(GL_BLEND_DST_ALPHA, &bdsta);
    p_glGetIntegerv(GL_BLEND_EQUATION_RGB, &beq); p_glGetIntegerv(GL_BLEND_EQUATION_ALPHA, &beqa);
    p_glGetIntegerv(GL_UNPACK_ALIGNMENT, &unpack); p_glGetIntegerv(GL_UNPACK_ROW_LENGTH, &rowlen);
    const GLboolean blend = p_glIsEnabled(GL_BLEND), depth = p_glIsEnabled(GL_DEPTH_TEST),
                    cull = p_glIsEnabled(GL_CULL_FACE), scis = p_glIsEnabled(GL_SCISSOR_TEST);

    const float d = s_pixel_density, s = (float)s_menu_scale;
    const float x0 = mx * d, y0 = my * d, x1 = x0 + (float)mw * s, y1 = y0 + (float)mh * s;
    const float X0 = x0 / w * 2 - 1, X1 = x1 / w * 2 - 1, Y0 = 1 - y0 / h * 2, Y1 = 1 - y1 / h * 2;
    const float q[] = { X0, Y0, 0, 0,  X1, Y0, 1, 0,  X0, Y1, 0, 1,  X1, Y1, 1, 1 };

    p_glViewport(0, 0, w, h);
    p_glDisable(GL_DEPTH_TEST); p_glDisable(GL_CULL_FACE); p_glDisable(GL_SCISSOR_TEST);
    p_glEnable(GL_BLEND);
    p_glBlendEquationSeparate(GL_FUNC_ADD, GL_FUNC_ADD);
    p_glBlendFuncSeparate(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA, GL_ONE, GL_ONE_MINUS_SRC_ALPHA);
    p_glUseProgram(g.prog);
    p_glUniform1i(p_glGetUniformLocation(g.prog, "img"), 0);
    p_glBindSampler(0, 0);
    p_glBindTexture(GL_TEXTURE_2D, g.tex);
    p_glPixelStorei(GL_UNPACK_ALIGNMENT, 4); p_glPixelStorei(GL_UNPACK_ROW_LENGTH, 0);
    p_glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, mw, mh, 0, GL_RGBA, GL_UNSIGNED_BYTE, s_img);
    p_glBindVertexArray(g.vao);
    p_glBindBuffer(GL_ARRAY_BUFFER, g.vbo);
    p_glBufferData(GL_ARRAY_BUFFER, sizeof q, q, GL_STREAM_DRAW);
    p_glDrawArrays(GL_TRIANGLE_STRIP, 0, 4);

    p_glBindBuffer(GL_ARRAY_BUFFER, (GLuint)abuf); p_glBindVertexArray((GLuint)vao);
    p_glBindTexture(GL_TEXTURE_2D, (GLuint)tex0); p_glBindSampler(0, (GLuint)samp);
    p_glActiveTexture((GLenum)atex); p_glUseProgram((GLuint)prog);
    p_glPixelStorei(GL_UNPACK_ALIGNMENT, unpack); p_glPixelStorei(GL_UNPACK_ROW_LENGTH, rowlen);
    p_glBlendEquationSeparate((GLenum)beq, (GLenum)beqa);
    p_glBlendFuncSeparate((GLenum)bsrc, (GLenum)bdst, (GLenum)bsrca, (GLenum)bdsta);
    if (!blend) p_glDisable(GL_BLEND);
    if (depth) p_glEnable(GL_DEPTH_TEST);
    if (cull) p_glEnable(GL_CULL_FACE);
    if (scis) p_glEnable(GL_SCISSOR_TEST);
    p_glViewport(vp[0], vp[1], vp[2], vp[3]);
}

#ifdef CH_MOUSE_SELFTEST
/* Test-only: CH_POINTER_SELFTEST_FILE holds lines pushed as real SDL events,
 * one line per frame ("wait N" holds N frames):
 *   move X Y | ldown | lup | rdown | rup | wheel DX DY |
 *   fdown ID X Y | fmove ID X Y | fup ID X Y
 * X/Y are window coordinates (finger lines convert to 0..1). */
#include <stdio.h>
static void selftest_step(void) {
    static FILE *f; static int wait;
    if (!f) { const char *n = getenv("CH_POINTER_SELFTEST_FILE"); if (!n || !(f = fopen(n, "r"))) return; }
    if (wait > 0) { --wait; return; }
    char line[128]; clearerr(f);
    if (!fgets(line, sizeof line, f)) return;
    int ww = 640, wh = 480; win_size(&ww, &wh);
    char cmd[16] = {0}; float a = 0, b = 0, c = 0; int n = sscanf(line, "%15s %f %f %f", cmd, &a, &b, &c);
    if (n < 1) return;
    SDL_Event e; SDL_zero(e);
    e.common.timestamp = SDL_GetTicksNS();
    if (!strcmp(cmd, "wait")) { wait = (int)a; return; }
    if (!strcmp(cmd, "move")) { e.type = SDL_EVENT_MOUSE_MOTION; e.motion.windowID = s_win; e.motion.x = a; e.motion.y = b; }
    else if (!strcmp(cmd, "ldown") || !strcmp(cmd, "lup") || !strcmp(cmd, "rdown") || !strcmp(cmd, "rup")) {
        e.type = cmd[1] == 'd' ? SDL_EVENT_MOUSE_BUTTON_DOWN : SDL_EVENT_MOUSE_BUTTON_UP;
        e.button.windowID = s_win; e.button.button = cmd[0] == 'l' ? SDL_BUTTON_LEFT : SDL_BUTTON_RIGHT;
        e.button.down = cmd[1] == 'd'; e.button.x = s_px; e.button.y = s_py; e.button.clicks = 1;
    } else if (!strcmp(cmd, "wheel")) {
        e.type = SDL_EVENT_MOUSE_WHEEL; e.wheel.windowID = s_win; e.wheel.which = 1;
        e.wheel.x = a; e.wheel.y = b; e.wheel.direction = SDL_MOUSEWHEEL_NORMAL;
        e.wheel.integer_x = (int)a; e.wheel.integer_y = (int)b; e.wheel.mouse_x = s_px; e.wheel.mouse_y = s_py;
    } else if (cmd[0] == 'f') {
        e.type = !strcmp(cmd, "fdown") ? SDL_EVENT_FINGER_DOWN : !strcmp(cmd, "fup") ? SDL_EVENT_FINGER_UP : SDL_EVENT_FINGER_MOTION;
        e.tfinger.windowID = s_win; e.tfinger.touchID = 1; e.tfinger.fingerID = (SDL_FingerID)a;
        e.tfinger.x = b / (float)ww; e.tfinger.y = c / (float)wh;
    } else return;
    SDL_PushEvent(&e);
    fprintf(stderr, "chptr %s menu=%d items=%d screen=%08X px=%.0f,%.0f t=%llu\n", cmd, s_menu_open, s_n_items, (unsigned)s_screen_ra, s_px, s_py, (unsigned long long)SDL_GetTicks());
}
#endif

void ch_pointer_set_screen(uint32_t ra) {
    lock(); s_screen_ra = ra; unlock();
}

void ch_pointer_set_list(int on) {
    lock(); s_list_on = on; unlock();
}

int ch_pointer_list_step(void) {
    lock(); const int st = s_list_step; unlock();
    return st;
}

int ch_pointer_enabled(void) {
    static int off = -1;
    if (off < 0) { const char *e = getenv("CH_POINTER"); off = (e && *e == '0') ? 1 : 0; }
    if (off || psx_netplay_active()) return 0;
    SDL_Window *win = s_win ? SDL_GetWindowFromID(s_win) : NULL;
    return !(win && SDL_GetWindowRelativeMouseMode(win));
}

void ch_pointer_frame(ChPointerFrame *out) {
    memset(out, 0, sizeof *out);
    if (!s_watch_on) {
        s_mu = SDL_CreateMutex();
        SDL_SetAtomicInt(&s_draws, 0);
        if (SDL_AddEventWatch(watch, NULL)) s_watch_on = 1;
        else return;
    }
    /* The HUD hook is cleared on session reset and GL shutdown: register again
     * whenever it has stopped being called. */
    const int draws = SDL_GetAtomicInt(&s_draws);
    if (draws != s_last_draws) { s_last_draws = draws; s_frames_since_draw = 0; }
    else if (++s_frames_since_draw > 60) s_hud_on = 0;
    if (!s_hud_on) { psx_host_set_hud_draw_cb(hud_draw, NULL); s_hud_on = 1; s_frames_since_draw = 0; }
#ifdef CH_MOUSE_SELFTEST
    selftest_step();
#endif
    lock();
    /* Touch long press opens the menu at the finger. */
    if (s_f1_down && !s_f_drag && !s_f_two && !s_f_consumed && !s_menu_open &&
        SDL_GetTicks() - s_f_t0 >= LONG_PRESS_MS) {
        s_f_consumed = 1; menu_open_at(s_px, s_py); s_activity = 1;
    }
    int ww = 0, wh = 0;
    if (win_size(&ww, &wh)) { s_ww = ww; s_wh = wh; }
    const float px = s_menu_open ? s_anchor_x : s_px, py = s_menu_open ? s_anchor_y : s_py;
    if (win_size(&ww, &wh)) {
        float rx, ry, rw, rh; game_rect(ww, wh, &rx, &ry, &rw, &rh);
        int x = (int)((px - rx) / rw * 512.0f), y = (int)((py - ry) / rh * 240.0f);
        if (x < 0x10) x = 0x10; else if (x > 0x1EF) x = 0x1EF;
        if (y < 0x08) y = 0x08; else if (y > 0xE7) y = 0xE7;
        out->x = x; out->y = y;
    }
    if (s_left_held || s_left_latch) out->buttons |= B_CROSS;
    s_left_latch = 0;
    if (s_fire_frames > 0) { out->buttons |= s_fire_bits; if (--s_fire_frames == 0) s_fire_bits = 0; }
    /* Mouse wheel: scroll the chip grid, or press a list's one-row arrow;
     * anywhere else it is dropped. */
    s_list_step = 0;
    if (s_screen_ra >= CHIP_GRID_START && s_screen_ra < CHIP_GRID_END && !s_menu_open) {
        uint32_t dir = 0;
        if (s_wheel_y > 0)      { dir |= B_UP;    --s_wheel_y; }
        else if (s_wheel_y < 0) { dir |= B_DOWN;  ++s_wheel_y; }
        if (s_wheel_x > 0)      { dir |= B_RIGHT; --s_wheel_x; }
        else if (s_wheel_x < 0) { dir |= B_LEFT;  ++s_wheel_x; }
        if (dir) { out->buttons |= B_R1 | dir; s_activity = 1; }
        s_wheel_n = 0;
    } else if (s_list_on && !s_menu_open && !s_left_held) {
        if (s_list_hold > 0) {
            --s_list_hold; s_list_step = s_list_dir; out->buttons |= B_CROSS;
        } else if (s_list_gap > 0) {
            --s_list_gap;
        } else if (s_wheel_n) {
            s_list_dir = s_wheel_n > 0 ? -1 : 1;
            s_wheel_n += s_wheel_n > 0 ? -1 : 1;
            s_list_step = s_list_dir; out->buttons |= B_CROSS;
            s_list_hold = LIST_PRESS - 1; s_list_gap = LIST_GAP;
            s_activity = 1;
        }
        s_wheel_x = s_wheel_y = 0; s_wheel_ax = 0.0f;
    } else {
        s_wheel_x = s_wheel_y = s_wheel_n = 0; s_wheel_ax = s_wheel_ay = 0.0f;
        s_list_hold = s_list_gap = 0;
    }
    out->activity = s_activity || out->buttons;
    s_activity = 0;
    unlock();
}

#else /* SDL2 builds: no pointer mode */
int  ch_pointer_enabled(void) { return 0; }
void ch_pointer_set_screen(uint32_t ra) { (void)ra; }
void ch_pointer_set_list(int on) { (void)on; }
int  ch_pointer_list_step(void) { return 0; }
void ch_pointer_frame(ChPointerFrame *out) { memset(out, 0, sizeof *out); }
#endif
