/* Carnage Heart (USA, SCUS-94604): restore PlayStation Mouse support.
 *
 * The USA executable still contains the menu code for a mouse cursor
 * (mouse-mode flag at 0x800DF358, cursor X/Y at 0x800DF368/0x800DF36C), but
 * its controller routine func_8003E154 had the branch that reads the mouse
 * removed. Carnage Heart EZ (Japan, SLPS-00919) still has that branch in
 * MAIN.EXE at 0x80042168 with the same variable layout (EZ: flag 0x800CC6D0,
 * cursor 0x800CC6E0/E4). This replacement is the USA routine with EZ's mouse
 * branch restored, translated to USA addresses. Non-mouse controllers behave
 * exactly as the original USA code.
 *
 * With a normal pad on port 1, the window pointer and touch screen drive the
 * same cursor alongside the pad (src/pointer_menu.c). */
#include <stdint.h>
#include "cpu_state.h"
#include "pointer_menu.h"

#ifdef CH_MOUSE_SELFTEST
/* Test-only: fake a PS1 Mouse on port 1. Each line appended to the file named
 * by CH_MOUSE_SELFTEST_FILE is "dx dy left right frames"; it is applied for
 * that many calls. Never compiled into normal builds. */
#include <stdio.h>
#include <stdlib.h>
static FILE *st_f; static int st_left, st_dx, st_dy, st_l, st_r;
static int selftest_report(uint8_t out[6]) {
    if (!st_f) { const char *n = getenv("CH_MOUSE_SELFTEST_FILE"); if (!n || !(st_f = fopen(n, "r"))) return 0; }
    if (st_left <= 0) { clearerr(st_f); if (fscanf(st_f, "%d %d %d %d %d", &st_dx, &st_dy, &st_l, &st_r, &st_left) != 5) { st_dx = st_dy = st_l = st_r = 0; st_left = 0; } }
    out[0] = 0; out[1] = 0x12; out[2] = 0xFF;
    out[3] = (uint8_t)(0xF0 | (st_r ? 0 : 0x04) | (st_l ? 0 : 0x08));
    out[4] = (uint8_t)(int8_t)st_dx; out[5] = (uint8_t)(int8_t)st_dy;
    if (st_left > 0) st_left--; else st_dx = st_dy = 0;
    return 1;
}
#endif

#define PAD1_BUF      0x800EEA64u /* InitPAD port-1 receive buffer */
#define PAD_BUTTONS   0x800E0134u /* current button word */
#define PAD_PREV      0x800E0044u /* previous button word */
#define MOUSE_MODE    0x800DF358u /* 1 = last report was a mouse */
#define REPEAT_FLAG   0x800DF364u
#define CURSOR_X      0x800DF368u /* 16..495 */
#define CURSOR_Y      0x800DF36Cu /* 8..231 */

/* Set when the pad takes over from the pointer; the hotspot navigator then
 * moves the cursor onto a box once (see func_8003E89C). */
static int nav_resnap;

static int32_t ez_signed_x(uint8_t b) { return b < 0x81 ? (int32_t)b : -(int32_t)(uint8_t)~b; }
static int32_t ez_signed_y(uint8_t b) { return b < 0x81 ? (int32_t)(b >> 1) : -(int32_t)((b >> 1) ^ 0x7F); }

void func_8003E154(CPUState *cpu)
{
#ifdef CH_MOUSE_SELFTEST
    { uint8_t r[6]; if (selftest_report(r)) for (int i = 0; i < 6; ++i) cpu->write_byte(PAD1_BUF + i, r[i]); }
#endif
    uint8_t type = cpu->read_byte(PAD1_BUF + 1);
    uint32_t buttons = cpu->read_word(PAD_BUTTONS);
    cpu->write_word(PAD_PREV, buttons);

    const int real_mouse = (type & 0xF0u) == 0x10u;
    if (!real_mouse && ch_pointer_enabled()) {
        /* Host pointer mode (src/pointer_menu.c): the pad on port 1 keeps all
         * its buttons, and the window pointer or touch drives the game's own
         * mouse cursor. Whichever was used last decides the mode: moving or
         * clicking the pointer turns mouse mode on (free cursor, click acts on
         * the box under it); a new pad press turns it off, so the D-pad jumps
         * between boxes as on the original. */
        static uint32_t prev_pad;
        static int pointer_mode;
        uint8_t b2 = cpu->read_byte(PAD1_BUF + 2), b3 = cpu->read_byte(PAD1_BUF + 3);
        uint32_t pad = (((uint32_t)b2 << 8) ^ 0xFF00u) + (uint8_t)~b3;
        ChPointerFrame pf;
        ch_pointer_frame(&pf);
        if (pf.activity) pointer_mode = 1;
        if ((pad & ~prev_pad) && pointer_mode) { pointer_mode = 0; nav_resnap = 1; }
        prev_pad = pad;
        buttons = pad | pf.buttons;
        cpu->write_word(PAD_BUTTONS, buttons);
        cpu->write_word(MOUSE_MODE, pointer_mode ? 1u : 0u);
        if (pointer_mode) {
            cpu->write_word(CURSOR_X, (uint32_t)pf.x);
            cpu->write_word(CURSOR_Y, (uint32_t)pf.y);
        } else {
            cpu->write_word(cpu->gpr[28] + 0xC10u, 0);
        }
    } else if (real_mouse && cpu->read_word(MOUSE_MODE) != 0) {
        /* EZ maps left -> 0x20 (Circle) and right -> 0x40 (Cross), because in
         * the Japanese release Circle confirms. The USA release swapped the
         * roles (Cross 0x40 confirms, Circle 0x20 backs out), including in the
         * leftover mouse-only code paths. So here left -> 0x40 (confirm) and
         * right -> 0x20 (back / open menu), which matches EZ's behaviour. */
        uint8_t b3 = (uint8_t)~cpu->read_byte(PAD1_BUF + 3);
        buttons = ((uint32_t)(b3 << 3) & 0x40u) | ((uint32_t)(b3 << 3) & 0x20u);
        cpu->write_word(PAD_BUTTONS, buttons);
        int32_t x = (int32_t)cpu->read_word(CURSOR_X) + ez_signed_x(cpu->read_byte(PAD1_BUF + 4));
        int32_t y = (int32_t)cpu->read_word(CURSOR_Y) + ez_signed_y(cpu->read_byte(PAD1_BUF + 5));
        if (x < 0x10) x = 0x10; else if (x >= 0x1F0) x = 0x1EF;
        if (y < 0x08) y = 0x08; else if (y >= 0xE8) y = 0xE7;
        cpu->write_word(CURSOR_X, (uint32_t)x);
        cpu->write_word(CURSOR_Y, (uint32_t)y);
        cpu->write_word(MOUSE_MODE, (type & 0x10u) ? 1u : 0u);
    } else if (cpu->read_word(MOUSE_MODE) == 0) {
        /* Original USA digital path. */
        uint8_t b2 = cpu->read_byte(PAD1_BUF + 2), b3 = cpu->read_byte(PAD1_BUF + 3);
        buttons = (((uint32_t)b2 << 8) ^ 0xFF00u) + (uint8_t)~b3;
        cpu->write_word(PAD_BUTTONS, buttons);
        cpu->write_word(MOUSE_MODE, (type & 0x10u) ? 1u : 0u);
        cpu->write_word(cpu->gpr[28] + 0xC10u, 0);
    } else {
        /* Original USA path for a flagged non-mouse device: no input. */
        cpu->write_word(MOUSE_MODE, (type & 0x10u) ? 1u : 0u);
        cpu->write_word(PAD_PREV, cpu->read_word(PAD_PREV) | 0xFFu);
        cpu->write_word(PAD_BUTTONS, 0);
    }

    /* Shared tail: hold-repeat handling (unchanged). */
    buttons = cpu->read_word(PAD_BUTTONS);
    if ((buttons & 0xFFFu) != 0 && cpu->read_word(REPEAT_FLAG) == 1u)
        cpu->write_word(PAD_BUTTONS, buttons & 0xF000u);
    else
        cpu->write_word(REPEAT_FLAG, 0);

    cpu->pc = cpu->gpr[31]; /* jr $ra */
}

/* Hotspot navigator (USA 0x8003E89C, EZ 0x8004290C). Each frame the menus call
 * it with a table of clickable boxes (id, x, y, w, h). For the D-pad it jumps
 * the cursor between boxes, but it also snaps the cursor to the centre of
 * whatever box the cursor is over. EZ does the same. With a mouse that makes
 * the pointer magnetic: small moves are pulled back every frame, so it is hard
 * to leave a box or reach the one you want. In mouse mode we run the original
 * and then put the cursor back where the mouse left it. The click still acts
 * on the box under the pointer, because callers hit-test the cursor themselves
 * (0x8003EEC4) before calling this. D-pad behaviour is unchanged.
 * The generated body can return early (interrupt bail) and be resumed later
 * through this same entry point, so the restore waits until it really returns. */
void ch_mouse_lle_func_8003E89C(CPUState *cpu);

#define NAV_ENTRY 0x8003E89Cu
static int nav_pending;
static uint32_t nav_ra, nav_x, nav_y;

void func_8003E89C(CPUState *cpu)
{
    const int fresh = (cpu->pc == 0u || cpu->pc == NAV_ENTRY);
    if (fresh) {
        nav_pending = cpu->read_word(MOUSE_MODE) != 0;
        nav_ra = cpu->gpr[31];
        nav_x = cpu->read_word(CURSOR_X);
        nav_y = cpu->read_word(CURSOR_Y);
#ifdef CH_MOUSE_SELFTEST
        if (getenv("CH_MOUSE_SELFTEST_SNAP")) nav_pending = 0; /* test: original snapping */
        { /* test: log each new hotspot table (caller, origin, boxes) */
            static uint32_t last_t, last_ra;
            uint32_t t = cpu->gpr[6];
            if (t != last_t || nav_ra != last_ra) {
                last_t = t; last_ra = nav_ra;
                fprintf(stderr, "chnav ra=%08X org=%d,%d table=%08X:", nav_ra, (int)cpu->gpr[4], (int)cpu->gpr[5], t);
                for (int k = 0; k < 48; ++k) {
                    uint32_t e = t + (uint32_t)k * 0x12u;
                    uint16_t id = cpu->read_half(e);
                    if (!id) break;
                    fprintf(stderr, " %u@%u,%u,%ux%u", id, cpu->read_half(e + 2), cpu->read_half(e + 4),
                            cpu->read_half(e + 6), cpu->read_half(e + 8));
                }
                fprintf(stderr, "\n");
            }
        }
#endif
    }
    if (fresh && !nav_pending && nav_resnap) {
        nav_resnap = 0;
        /* Pad mode after the pointer was used: the cursor may sit outside
         * every box (the D-pad can only move from a box it is on), so put it
         * on the centre of the nearest box first. Only once, right after the
         * switch, so screens with a free D-pad cursor are left alone. */
        const int32_t ox = (int32_t)cpu->gpr[4], oy = (int32_t)cpu->gpr[5];
        const int32_t cx = (int32_t)cpu->read_word(CURSOR_X), cy = (int32_t)cpu->read_word(CURSOR_Y);
        const uint32_t t = cpu->gpr[6];
        int inside = 0, have = 0; int64_t best = 0; int32_t bx = 0, by = 0;
        for (int k = 0; k < 64; ++k) {
            const uint32_t e = t + (uint32_t)k * 0x12u;
            if (!cpu->read_half(e)) break;
            const int32_t x = ox + cpu->read_half(e + 2), y = oy + cpu->read_half(e + 4);
            const int32_t w = cpu->read_half(e + 6), h = cpu->read_half(e + 8);
            if (cx >= x && cx < x + w && cy >= y && cy < y + h) { inside = 1; break; }
            const int32_t mx = x + w / 2, my = y + h / 2;
            const int64_t d = (int64_t)(mx - cx) * (mx - cx) + (int64_t)(my - cy) * (my - cy);
            if (!have || d < best) { have = 1; best = d; bx = mx; by = my; }
        }
        if (!inside && have) {
            cpu->write_word(CURSOR_X, (uint32_t)bx);
            cpu->write_word(CURSOR_Y, (uint32_t)by);
        }
    }
    ch_mouse_lle_func_8003E89C(cpu);
    if (nav_pending && cpu->pc == nav_ra) {
        cpu->write_word(CURSOR_X, nav_x);
        cpu->write_word(CURSOR_Y, nav_y);
        nav_pending = 0;
    }
}
