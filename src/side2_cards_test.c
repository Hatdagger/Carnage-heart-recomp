/*
 * Experiment (not for release): fight a campaign battle as a VS-mode battle,
 * so side 2 uses PLAYER designs (full cards with chip programs).
 *
 * With CH_SIDE2_PLAYER=1: when the 3D battle func_8001CF48 is entered in
 * campaign mode (0x800DF3D0 == 1) it switches the mode to 2 (VS) and leaves
 * it there; func_80035CD4 writes 0 when the battle ends. The test also needs
 * side 2's OKEs set to owner 1 with a player card index (done by hand over
 * the debug server). CH_SIDE2_STAGE=n optionally forces VS battlefield n.
 *
 * Build with -DCH_SIDE2_TEST=ON. Details: docs/research/VS_MODE_AND_SAVES.md
 */
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include "cpu_state.h"

#define BATTLE_MODE 0x800DF3D0u

void ch_side2_lle_func_8001CF48(CPUState *cpu);

static uint32_t rd32(CPUState *cpu, uint32_t a) { return cpu->read_word(a); }
static void wr32(CPUState *cpu, uint32_t a, uint32_t v) { cpu->write_word(a, v); }

void func_8001CF48(CPUState *cpu)
{
    static int on = -1;
    if (on < 0) {
        const char *e = getenv("CH_SIDE2_PLAYER");
        on = (e && *e == '1');
    }
    uint32_t mode = rd32(cpu, BATTLE_MODE);
    if (on && mode == 1) {
        fprintf(stderr, "[side2-test] 3D battle: running campaign battle in VS mode (2)\n");
        const char *st = getenv("CH_SIDE2_STAGE");
        wr32(cpu, BATTLE_MODE, 2);
        if (st) wr32(cpu, BATTLE_MODE + 4, (uint32_t)atoi(st));
        /* Leave mode 2 set: the battle is entered/left several times, and
         * func_80035CD4 itself writes 0 to the mode when the battle ends. */
        ch_side2_lle_func_8001CF48(cpu);
        return;
    }
    ch_side2_lle_func_8001CF48(cpu);
}

