/* Carnage Heart (USA, SCUS-94604): restore PlayStation Mouse support.
 *
 * The USA executable still contains the menu code for a mouse cursor
 * (mouse-mode flag at 0x800DF358, cursor X/Y at 0x800DF368/0x800DF36C), but
 * its controller routine func_8003E154 had the branch that reads the mouse
 * removed. Carnage Heart EZ (Japan, SLPS-00919) still has that branch in
 * MAIN.EXE at 0x80042168 with the same variable layout (EZ: flag 0x800CC6D0,
 * cursor 0x800CC6E0/E4). This replacement is the USA routine with EZ's mouse
 * branch restored, translated to USA addresses. Non-mouse controllers behave
 * exactly as the original USA code. */
#include <stdint.h>
#include "cpu_state.h"

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

    if ((type & 0xF0u) == 0x10u && cpu->read_word(MOUSE_MODE) != 0) {
        /* EZ mouse branch: left button -> 0x20 (Circle), right -> 0x40 (Cross). */
        uint8_t b3 = (uint8_t)~cpu->read_byte(PAD1_BUF + 3);
        buttons = ((uint32_t)(b3 << 4) & 0x40u) | ((uint32_t)(b3 << 2) & 0x20u);
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
