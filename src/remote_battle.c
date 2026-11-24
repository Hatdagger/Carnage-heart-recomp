/*
 * Remote-player battles (multiplayer groundwork; off unless built with
 * -DCH_REMOTE_BATTLE=ON and switched on at run time).
 *
 * A campaign battle normally puts CPU stat cards on side 2. For campaign
 * versus, side 2 is another human player whose OKEs are full designs with
 * their own chip programs. This runs such a battle the way VS mode does:
 *
 *   before   - copy the remote designs into spare player card slots (slots
 *              that side 1 does not use in this battle), backing them up;
 *            - mark side 2's OKEs as player designs (owner 1, card = slot).
 *   battle   - the 3D battle (func_8001CF48) runs in VS mode (2) for the
 *              whole battle; func_80035CD4 writes 0 when it ends.
 *   after    - (just before the result code func_80035988 runs, since the
 *              battle is fought after func_80035CD4 returns)
 *              side 2's OKEs go back to owner 2 with their original card
 *              index (HP, kills and ammo from the battle are kept), and the
 *              borrowed card slots are restored.
 * The game's own code then removes destroyed OKEs, deletes beaten units,
 * retreats the loser and changes base ownership, as for any battle.
 *
 * Run-time switches (test harness until the turn-file layer exists):
 *   CH_REMOTE_BATTLE=1        treat every CPU unit as a remote player's unit
 *   CH_REMOTE_DESIGNS=<file>  1152-byte design records (e.g. DATA/DEFRP.CRD
 *                             from the disc = MACHINE01-03); OKE n of the
 *                             unit uses record n mod count
 *   CH_REMOTE_FRESH=0         keep the OKEs' current HP/ammo instead of
 *                             setting fresh values from the design (the
 *                             default, needed while the map OKEs are CPU ones)
 *   CH_HOTSEAT=1              hot-seat (hotseat.c): every battle uses the
 *                             waiting side's own designs (by each OKE's card
 *                             index) and keeps the OKEs' HP and ammo
 * Details: docs/research/VS_MODE_AND_SAVES.md
 */
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "cpu_state.h"

/* Note on hooks: a generated function is entered again with cpu->pc set to
 * a resume address when the game waits for frames inside it. The wrappers
 * act only on fresh calls (cpu->pc == 0) and pass resumes straight through. */

#define BATTLE_MODE     0x800DF3D0u /* 0 none, 1 campaign, 2 VS */
#define PTR_PLAYER_CARD 0x800E0014u
#define PTR_OKE_TABLE   0x800E014Cu
#define PTR_UNIT_TABLE  0x800E017Cu
#define PARTS_TABLE     0x800C0C84u /* 36-byte part records */
#define CARD_SIZE       1152u
#define CARD_SLOTS      28
#define OKE_SIZE        12u
#define UNIT_SIZE       22u
#define MAX_DESIGNS     28

void ch_remote_lle_func_8001CF48(CPUState *cpu);
void ch_remote_lle_func_80035CD4(CPUState *cpu);
void ch_remote_lle_func_80035988(CPUState *cpu);

/* Hot-seat (hotseat.c): side 2's OKEs use the waiting side's own designs. */
int ch_hotseat_active(void);
const uint8_t *ch_hotseat_other_card(int idx);
/* Turn files (turn_files.c): record the result for the turn file. */
void ch_turns_note_battle(CPUState *cpu, uint32_t unit1, uint32_t unit2);

static int g_enabled = -1;
static int g_fresh = 1;
static int g_design_count;
static uint8_t g_designs[MAX_DESIGNS][CARD_SIZE];
static int g_active;               /* a remote battle is in progress */
static uint32_t g_unit1, g_unit2;  /* the two units in it */

/* What to put back when the battle is over. */
static int16_t g_oke2[3];
static uint8_t g_saved_oke[3][OKE_SIZE];
static int g_borrowed[3], g_nborrowed;
static uint8_t g_saved_card[3][CARD_SIZE];

static void load_config(void)
{
    const char *e = getenv("CH_REMOTE_BATTLE");
    g_enabled = (e && *e == '1');
    e = getenv("CH_REMOTE_FRESH");
    g_fresh = !(e && *e == '0');
    if (ch_hotseat_active()) {
        /* Designs come from the waiting side's card table; the OKEs already
         * carry their own HP and ammo. */
        g_enabled = 1;
        g_fresh = 0;
        fprintf(stderr, "[remote-battle] on (hot-seat designs)\n");
        return;
    }
    if (!g_enabled)
        return;
    e = getenv("CH_REMOTE_DESIGNS");
    FILE *f = e ? fopen(e, "rb") : NULL;
    if (f) {
        while (g_design_count < MAX_DESIGNS &&
               fread(g_designs[g_design_count], 1, CARD_SIZE, f) == CARD_SIZE)
            g_design_count++;
        fclose(f);
    }
    fprintf(stderr, "[remote-battle] on, %d design(s) from %s\n",
            g_design_count, e ? e : "(none)");
    if (g_design_count == 0)
        g_enabled = 0;
}

static void read_block(CPUState *cpu, uint32_t a, uint8_t *dst, uint32_t n)
{
    for (uint32_t i = 0; i < n; i++) dst[i] = cpu->read_byte(a + i);
}
static void write_block(CPUState *cpu, uint32_t a, const uint8_t *src, uint32_t n)
{
    for (uint32_t i = 0; i < n; i++) cpu->write_byte(a + i, src[i]);
}

/* The 3D battle: keep VS mode for the whole remote battle. */
void func_8001CF48(CPUState *cpu)
{
    if (g_active && cpu->pc == 0 && cpu->read_word(BATTLE_MODE) == 1)
        cpu->write_word(BATTLE_MODE, 2);
    ch_remote_lle_func_8001CF48(cpu);
}

/* Turn files switch remote battles off when the wrong scenario was started
 * (the map is then a normal campaign). */
void ch_remote_battle_off(void)
{
    g_enabled = 0;
}

/* Campaign battle set-up: a0 = side-1 (player) unit, a1 = side-2 unit. */
void func_80035CD4(CPUState *cpu)
{
    if (g_enabled < 0)
        load_config();
    /* cpu->pc != 0: the generated code is resuming this function part-way
     * (the battle waits for frames inside it). Only a fresh call is a new
     * battle. */
    if (!g_enabled || cpu->pc != 0 || g_active) {
        ch_remote_lle_func_80035CD4(cpu);
        return;
    }

    const uint32_t unit1 = cpu->gpr[4], unit2 = cpu->gpr[5];
    const uint32_t units = cpu->read_word(PTR_UNIT_TABLE);
    const uint32_t okes  = cpu->read_word(PTR_OKE_TABLE);
    const uint32_t cards = cpu->read_word(PTR_PLAYER_CARD);
    const uint32_t u1 = units + unit1 * UNIT_SIZE, u2 = units + unit2 * UNIT_SIZE;

    /* Card slots side 1 uses in this battle may not be borrowed. */
    uint8_t used[CARD_SLOTS] = {0};
    for (int s = 0; s < 3; s++) {
        int16_t k = (int16_t)cpu->read_half(u1 + 0x0C + 2 * s);
        if (k < 0) continue;
        uint8_t c = cpu->read_byte(okes + k * OKE_SIZE + 3);
        if (c < CARD_SLOTS) used[c] = 1;
    }

    int16_t *oke2 = g_oke2;
    const int hotseat = ch_hotseat_active();
    int slot_of_design[MAX_DESIGNS];
    g_nborrowed = 0;
    for (int d = 0; d < MAX_DESIGNS; d++) slot_of_design[d] = -1;

    for (int s = 0; s < 3; s++) {
        oke2[s] = (int16_t)cpu->read_half(u2 + 0x0C + 2 * s);
        if (oke2[s] < 0) continue;
        const uint32_t o = okes + oke2[s] * OKE_SIZE;
        read_block(cpu, o, g_saved_oke[s], OKE_SIZE);

        /* Hot-seat: the OKE's own card index in the waiting side's table.
         * Otherwise: the test designs, round robin. */
        const int d = hotseat ? (g_saved_oke[s][3] % MAX_DESIGNS) : s % g_design_count;
        const uint8_t *design = hotseat ? ch_hotseat_other_card(d) : g_designs[d];
        if (!design) { oke2[s] = -1; continue; }
        if (slot_of_design[d] < 0) {
            int slot = -1;
            for (int c = CARD_SLOTS - 1; c >= 0; c--)
                if (!used[c]) { slot = c; break; }
            if (slot < 0) { oke2[s] = -1; continue; }
            used[slot] = 1;
            g_borrowed[g_nborrowed] = slot;
            read_block(cpu, cards + slot * CARD_SIZE, g_saved_card[g_nborrowed], CARD_SIZE);
            write_block(cpu, cards + slot * CARD_SIZE, design, CARD_SIZE);
            g_nborrowed++;
            slot_of_design[d] = slot;
        }
        const int slot = slot_of_design[d];
        cpu->write_byte(o + 0, 1);              /* owner: player design */
        cpu->write_byte(o + 3, (uint8_t)slot);  /* card index */
        if (g_fresh) {
            /* Same values the game gives a newly built player OKE (0x8005B118). */
            const uint8_t *c = design;
            cpu->write_half(o + 4, 0);                               /* kills */
            cpu->write_half(o + 6, (uint16_t)(c[52] | (c[53] << 8))); /* HP */
            cpu->write_half(o + 8, c[54]);
            cpu->write_byte(o + 10, cpu->read_byte(PARTS_TABLE + (c[45] + 0xC3u) * 36u)); /* main ammo */
            cpu->write_byte(o + 11, cpu->read_byte(PARTS_TABLE + (c[38] + 0x7Au) * 36u)); /* sub ammo */
        }
    }

    fprintf(stderr, "[remote-battle] unit %u vs remote unit %u, borrowed %d card slot(s)\n",
            unit1, unit2, g_nborrowed);
    /* The battle runs over the following frames; func_80035CD4 can return to
     * us before it is fought. The "after" step runs from the result code
     * (func_80035988), which the game calls once the battle is over. */
    g_unit1 = unit1;
    g_unit2 = unit2;
    g_active = 1;
    ch_remote_lle_func_80035CD4(cpu);
}

static void finish_remote_battle(CPUState *cpu)
{
    const uint32_t okes  = cpu->read_word(PTR_OKE_TABLE);
    const uint32_t cards = cpu->read_word(PTR_PLAYER_CARD);
    int16_t *oke2 = g_oke2;

    for (int s = 0; s < 3; s++) {
        if (oke2[s] < 0) continue;
        const uint32_t o = okes + oke2[s] * OKE_SIZE;
        cpu->write_byte(o + 0, g_saved_oke[s][0]);  /* owner back to the CPU side */
        cpu->write_byte(o + 3, g_saved_oke[s][3]);  /* original card index */
        fprintf(stderr, "[remote-battle]  remote OKE %d: HP %d, kills %d\n", oke2[s],
                (int16_t)cpu->read_half(o + 6), (int16_t)cpu->read_half(o + 4));
    }
    for (int i = 0; i < g_nborrowed; i++)
        write_block(cpu, cards + g_borrowed[i] * CARD_SIZE, g_saved_card[i], CARD_SIZE);
    ch_turns_note_battle(cpu, g_unit1, g_unit2);
    g_active = 0;
}

/* Battle result code, called for both units after every battle. */
void func_80035988(CPUState *cpu)
{
    if (g_active && cpu->pc == 0)
        finish_remote_battle(cpu);
    ch_remote_lle_func_80035988(cpu);
}
