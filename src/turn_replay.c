/*
 * The game's rand() while playing online (turn files, built with
 * -DCH_REMOTE_BATTLE=ON).
 *
 * The game calls rand() through one stub, 0x8008E1E4 (BIOS A0:2F, 140
 * callers; battles use it). To fight an opponent's battle again in the
 * replay of their turn (turn_files.c), the battle has to start from the same
 * random seed on both devices. So while online play is on, this stub keeps
 * its own seed, with the same formula as the BIOS
 * (seed = seed * 0x41C64E6D + 0x3039; result = (seed >> 16) & 0x7FFF), and
 * every battle starts from a seed that is written into the turn file.
 * Otherwise the BIOS's rand() runs as before.
 */
#include <stdint.h>
#include "cpu_state.h"

void ch_remote_lle_func_8008E1E4(CPUState *cpu);
int ch_turns_on(void);

static uint32_t g_seed = 0x24040001u;

uint32_t ch_rand_get(void) { return g_seed; }
void ch_rand_set(uint32_t seed) { g_seed = seed; }

void func_8008E1E4(CPUState *cpu)
{
    if (!ch_turns_on() || cpu->pc != 0) {
        ch_remote_lle_func_8008E1E4(cpu);
        return;
    }
    g_seed = g_seed * 0x41C64E6Du + 0x3039u;
    cpu->gpr[2] = (g_seed >> 16) & 0x7FFFu;
    cpu->pc = cpu->gpr[31];          /* jr $ra */
}
