/*
 * Hot-seat campaign versus (multiplayer plan step 4; built with
 * -DCH_REMOTE_BATTLE=ON, switched on at run time with CH_HOTSEAT=1).
 *
 * Two people share one copy of the game and take turns. Side A starts as
 * "the player" (units 0-31, owner 1); side B takes over the CPU's side.
 * Each side always plays as "the player": at the end of every turn the
 * two sides are swapped in RAM, so the game's own order screens, factory,
 * movement and battle code serve whoever's turn it is.
 *
 * End Turn (0x8001B8EC) with hot-seat on:
 *   1. the current side's units move and fight (the game's own loop);
 *      battles against the other side's units are fought with that side's
 *      designs (remote_battle.c, using ch_hotseat_other_card());
 *   2. the CPU's brain (0x8001C268: CPU production, forming and ordering
 *      CPU units) is skipped, and the waiting side's units are hidden from
 *      the CPU-unit loop so they do not move on the other side's turn;
 *   3. the rest of End Turn runs as usual (base fuel, research and
 *      negotiation, production, resupply, base repair). It is written for
 *      "player" and "CPU", so it would also touch the waiting side, with
 *      CPU rules (base repair caps HP at the CPU card's value, which is 0
 *      for a design index; the CPU's research spends its funds). So the
 *      waiting side's units, bases, OKEs and funds are snapshotted at
 *      step 2 and put back at step 4: each side gets fuel, repair,
 *      resupply and production once a round, on its own turn;
 *   4. just before the win/lose check (0x8008A2B8, called only from End
 *      Turn, after the bases are counted) the sides are swapped:
 *        units i <-> i+32 (i = 0..31), owner bytes 1 <-> 2 in units, bases
 *        and OKEs, funds (gp+52 <-> gp+56), the player card table (the
 *        waiting side's designs are kept here), and the selected base.
 *      The turn counter goes up once per round, not once per side.
 *      When a side has no bases left there is no swap, so the game's win or
 *      lose screens are shown to the side that just played.
 *
 * Set-up, on the first End Turn: side B gets a copy of side A's card table
 * (the stock designs), the CPU's OKEs become side-B OKEs built from the
 * stock designs MACHINE01-03 (cards 25-27, fresh HP and ammo), side B's
 * funds are set to side A's, and the CPU bases' production lines are
 * cleared.
 *
 * Turn files (turn_files.c, CH_TURN_DIR): the same machinery between two
 * copies of the game. At the swap point, instead of swapping, this copy
 * sends the state after its half and loads the opponent's next turn.
 *
 * Known gaps (fine for a local test, to fix for turn files):
 *   - research and negotiation (0x8004BBB8, not mapped yet) run for the
 *     CPU on every End Turn; only the waiting side's funds are protected;
 *   - kills and losses of the waiting side's OKEs are added to the CPU card
 *     records, not to that side's designs;
 *   - this state lives in the module, not in RAM: savestates and save files
 *     do not carry it.
 */
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "cpu_state.h"

#define GP_TURN         0x800DF37Cu
#define GP_TURN2        0x800DF380u
#define GP_FUNDS_PLAYER 0x800DF38Cu
#define GP_FUNDS_CPU    0x800DF390u
#define GP_CUR_BASE     0x800DF404u
#define PTR_PLAYER_CARD 0x800E0014u
#define PTR_OKE_TABLE   0x800E014Cu
#define PTR_BASE_TABLE  0x800E0154u
#define PTR_UNIT_TABLE  0x800E017Cu
#define PARTS_TABLE     0x800C0C84u
#define CARD_SIZE       1152u
#define CARD_SLOTS      28
#define OKE_SIZE        12u
#define OKE_COUNT       200
#define BASE_SIZE       92u
#define BASE_COUNT      16
#define UNIT_SIZE       22u
#define STOCK_CARD      25   /* MACHINE01; 26, 27 = MACHINE02, 03 */

void ch_remote_lle_func_8001B8EC(CPUState *cpu);
void ch_remote_lle_func_8001C268(CPUState *cpu);
void ch_remote_lle_func_8001BCB0(CPUState *cpu);
void ch_remote_lle_func_8008A2B8(CPUState *cpu);

/* turn_files.c */
int ch_turns_active(void);
void ch_turns_begin(CPUState *cpu);
int ch_turns_swap_point(CPUState *cpu, int game_over);

static int g_on = -1;
static int g_ready;          /* set-up done */
static int g_side;           /* 0: side A is "the player", 1: side B */
static int g_hidden;         /* waiting side's units hidden from the CPU loop */
static uint8_t g_hidden_owner[32];
static uint8_t g_other_cards[CARD_SLOTS][CARD_SIZE];
/* The waiting side, as it was when the CPU phase would have started. */
static uint8_t g_snap_units[32][UNIT_SIZE];
static uint8_t g_snap_bases[BASE_COUNT][BASE_SIZE];
static uint8_t g_snap_okes[OKE_COUNT][OKE_SIZE];
static uint32_t g_snap_funds;
static int g_snapped;
static uint32_t g_other_base;

int ch_hotseat_active(void)
{
    if (g_on < 0) {
        const char *e = getenv("CH_HOTSEAT");
        g_on = (e && *e == '1') || ch_turns_active();
        if (g_on) fprintf(stderr, "[hotseat] on%s\n", ch_turns_active() ? " (turn files)" : "");
    }
    return g_on;
}

/* Turn files switch hot-seat off when the wrong scenario was started. */
void ch_hotseat_off(void)
{
    if (g_on > 0) fprintf(stderr, "[hotseat] off\n");
    g_on = 0;
}

const uint8_t *ch_hotseat_other_card(int idx)
{
    return (idx >= 0 && idx < CARD_SLOTS) ? g_other_cards[idx] : NULL;
}

uint8_t *ch_hotseat_other_cards(void)
{
    return &g_other_cards[0][0];
}

static void rd(CPUState *cpu, uint32_t a, uint8_t *d, uint32_t n)
{ for (uint32_t i = 0; i < n; i++) d[i] = cpu->read_byte(a + i); }
static void wr(CPUState *cpu, uint32_t a, const uint8_t *s, uint32_t n)
{ for (uint32_t i = 0; i < n; i++) cpu->write_byte(a + i, s[i]); }

static int first_base_of(CPUState *cpu, uint8_t owner)
{
    const uint32_t bases = cpu->read_word(PTR_BASE_TABLE);
    for (int b = 0; b < BASE_COUNT; b++)
        if (cpu->read_byte(bases + b * BASE_SIZE) == owner) return b;
    return 0;
}

static void setup(CPUState *cpu)
{
    const uint32_t cards = cpu->read_word(PTR_PLAYER_CARD);
    const uint32_t okes  = cpu->read_word(PTR_OKE_TABLE);
    const uint32_t bases = cpu->read_word(PTR_BASE_TABLE);

    for (int c = 0; c < CARD_SLOTS; c++)
        rd(cpu, cards + c * CARD_SIZE, g_other_cards[c], CARD_SIZE);

    int n = 0;
    for (int k = 0; k < OKE_COUNT; k++) {
        const uint32_t o = okes + k * OKE_SIZE;
        if (cpu->read_byte(o) != 2) continue;
        const int card = STOCK_CARD + (n++ % 3);
        const uint8_t *c = g_other_cards[card];
        /* Same values the game gives a newly built player OKE (0x8005B118). */
        cpu->write_byte(o + 3, (uint8_t)card);
        cpu->write_half(o + 4, 0);
        cpu->write_half(o + 6, (uint16_t)(c[52] | (c[53] << 8)));
        cpu->write_half(o + 8, c[54]);
        cpu->write_byte(o + 10, cpu->read_byte(PARTS_TABLE + (c[45] + 0xC3u) * 36u));
        cpu->write_byte(o + 11, cpu->read_byte(PARTS_TABLE + (c[38] + 0x7Au) * 36u));
    }
    for (int b = 0; b < BASE_COUNT; b++) {
        const uint32_t base = bases + b * BASE_SIZE;
        if (cpu->read_byte(base) != 2) continue;
        for (int l = 0; l < 6; l++) {
            const uint32_t line = base + 0x10 + 12 * l;
            cpu->write_byte(line + 0, 1);       /* nothing on order */
            cpu->write_byte(line + 1, 0xFF);    /* no product */
            cpu->write_half(line + 2, 0);
            cpu->write_half(line + 4, 0);
            cpu->write_half(line + 10, 0);
        }
    }
    cpu->write_word(GP_FUNDS_CPU, cpu->read_word(GP_FUNDS_PLAYER));
    g_other_base = (uint32_t)first_base_of(cpu, 2);
    g_side = 0;
    g_ready = 1;
    fprintf(stderr, "[hotseat] set up: %d OKE(s) given to side B, funds %u each\n",
            n, cpu->read_word(GP_FUNDS_PLAYER));
}

/* Turn files: set up as soon as the map opens, so the newest turn file can
 * be loaded before the player gives any orders (turn_files.c). */
void ch_hotseat_setup_now(CPUState *cpu)
{
    if (ch_hotseat_active() && !g_ready)
        setup(cpu);
}

static void snapshot_waiting_side(CPUState *cpu)
{
    const uint32_t okes  = cpu->read_word(PTR_OKE_TABLE);
    const uint32_t bases = cpu->read_word(PTR_BASE_TABLE);
    const uint32_t units = cpu->read_word(PTR_UNIT_TABLE);
    for (int i = 0; i < 32; i++)
        rd(cpu, units + (i + 32) * UNIT_SIZE, g_snap_units[i], UNIT_SIZE);
    for (int b = 0; b < BASE_COUNT; b++)
        rd(cpu, bases + b * BASE_SIZE, g_snap_bases[b], BASE_SIZE);
    for (int k = 0; k < OKE_COUNT; k++)
        rd(cpu, okes + k * OKE_SIZE, g_snap_okes[k], OKE_SIZE);
    g_snap_funds = cpu->read_word(GP_FUNDS_CPU);
    g_snapped = 1;
}

static void restore_waiting_side(CPUState *cpu)
{
    const uint32_t okes  = cpu->read_word(PTR_OKE_TABLE);
    const uint32_t bases = cpu->read_word(PTR_BASE_TABLE);
    const uint32_t units = cpu->read_word(PTR_UNIT_TABLE);
    for (int i = 0; i < 32; i++)
        wr(cpu, units + (i + 32) * UNIT_SIZE, g_snap_units[i], UNIT_SIZE);
    for (int b = 0; b < BASE_COUNT; b++)
        if (g_snap_bases[b][0] == 2) {
            wr(cpu, bases + b * BASE_SIZE, g_snap_bases[b], BASE_SIZE);
        }
    for (int k = 0; k < OKE_COUNT; k++)
        if (g_snap_okes[k][0] == 2) {
            wr(cpu, okes + k * OKE_SIZE, g_snap_okes[k], OKE_SIZE);
        }
    const uint32_t f = cpu->read_word(GP_FUNDS_CPU);
    if (f != g_snap_funds)
        fprintf(stderr, "[hotseat] waiting side's funds put back: %u -> %u\n", f, g_snap_funds);
    cpu->write_word(GP_FUNDS_CPU, g_snap_funds);
    g_snapped = 0;
}

static uint8_t swap_owner(uint8_t o) { return o == 1 ? 2 : o == 2 ? 1 : o; }

static void swap_sides(CPUState *cpu)
{
    const uint32_t cards = cpu->read_word(PTR_PLAYER_CARD);
    const uint32_t okes  = cpu->read_word(PTR_OKE_TABLE);
    const uint32_t bases = cpu->read_word(PTR_BASE_TABLE);
    const uint32_t units = cpu->read_word(PTR_UNIT_TABLE);
    uint8_t a[UNIT_SIZE], b[UNIT_SIZE];

    if (g_snapped)
        restore_waiting_side(cpu);
    for (int i = 0; i < 32; i++) {
        const uint32_t ua = units + i * UNIT_SIZE, ub = units + (i + 32) * UNIT_SIZE;
        rd(cpu, ua, a, UNIT_SIZE);
        rd(cpu, ub, b, UNIT_SIZE);
        a[0] = swap_owner(a[0]);
        b[0] = swap_owner(b[0]);
        wr(cpu, ua, b, UNIT_SIZE);
        wr(cpu, ub, a, UNIT_SIZE);
    }
    for (int k = 0; k < BASE_COUNT; k++) {
        const uint32_t p = bases + k * BASE_SIZE;
        cpu->write_byte(p, swap_owner(cpu->read_byte(p)));
    }
    for (int k = 0; k < OKE_COUNT; k++) {
        const uint32_t p = okes + k * OKE_SIZE;
        cpu->write_byte(p, swap_owner(cpu->read_byte(p)));
    }

    const uint32_t f1 = cpu->read_word(GP_FUNDS_PLAYER), f2 = cpu->read_word(GP_FUNDS_CPU);
    cpu->write_word(GP_FUNDS_PLAYER, f2);
    cpu->write_word(GP_FUNDS_CPU, f1);

    static uint8_t tmp[CARD_SIZE];
    for (int c = 0; c < CARD_SLOTS; c++) {
        rd(cpu, cards + c * CARD_SIZE, tmp, CARD_SIZE);
        wr(cpu, cards + c * CARD_SIZE, g_other_cards[c], CARD_SIZE);
        memcpy(g_other_cards[c], tmp, CARD_SIZE);
    }

    const uint32_t cur = cpu->read_word(GP_CUR_BASE);
    uint32_t next = g_other_base;
    if (next >= BASE_COUNT || cpu->read_byte(bases + next * BASE_SIZE) != 1)
        next = (uint32_t)first_base_of(cpu, 1);
    cpu->write_word(GP_CUR_BASE, next);
    g_other_base = cur;

    /* One turn number per round: side A's End Turn counted it already. */
    if (g_side == 0) {
        cpu->write_word(GP_TURN, cpu->read_word(GP_TURN) - 1);
        cpu->write_word(GP_TURN2, cpu->read_word(GP_TURN2) - 1);
    }
    g_side ^= 1;
    fprintf(stderr, "[hotseat] side %c to play, turn %u, funds %u (other side %u)\n",
            g_side ? 'B' : 'A', cpu->read_word(GP_TURN), f2, f1);
}

/* End Turn: set up on the first one. */
void func_8001B8EC(CPUState *cpu)
{
    if (cpu->pc == 0 && ch_hotseat_active()) {
        if (!g_ready)
            setup(cpu);
        if (ch_turns_active())
            ch_turns_begin(cpu);
    }
    ch_remote_lle_func_8001B8EC(cpu);
}

/* CPU brain: skipped. The battle phase is over: snapshot the waiting
 * side (put back just before the swap). Hide the waiting side's units from the loop that
 * follows (it only processes units with a nonzero owner byte). */
void func_8001C268(CPUState *cpu)
{
    if (!(ch_hotseat_active() && g_ready)) {
        ch_remote_lle_func_8001C268(cpu);
        return;
    }
    if (cpu->pc != 0) {             /* not expected: we never start it */
        ch_remote_lle_func_8001C268(cpu);
        return;
    }
    snapshot_waiting_side(cpu);
    const uint32_t units = cpu->read_word(PTR_UNIT_TABLE);
    for (int i = 0; i < 32; i++) {
        const uint32_t u = units + (i + 32) * UNIT_SIZE;
        g_hidden_owner[i] = cpu->read_byte(u);
        cpu->write_byte(u, 0);
    }
    g_hidden = 1;
    /* Calls are continuation-passing: a skipped function still has to
     * "return" (jr $ra) by handing control back to the caller. */
    cpu->pc = cpu->gpr[31];
}

/* Called after the CPU-unit loop (and inside both unit loops, but the
 * CPU-unit loop is empty while the units are hidden): unhide them. */
void func_8001BCB0(CPUState *cpu)
{
    if (g_hidden && cpu->pc == 0) {
        const uint32_t units = cpu->read_word(PTR_UNIT_TABLE);
        for (int i = 0; i < 32; i++)
            cpu->write_byte(units + (i + 32) * UNIT_SIZE, g_hidden_owner[i]);
        g_hidden = 0;
    }
    ch_remote_lle_func_8001BCB0(cpu);
}

/* Turn files: waiting for the opponent without pausing the game. While
 * waiting, this hook "calls" the game's VSync (one frame) with itself as the
 * return address, so the game keeps running frames (and the window and the
 * debug server keep responding), and checks for the file after each frame.
 * The registers End Turn had when it called the hook are kept and put back
 * when the file has been loaded, then the original function runs as if
 * nothing had happened. */
#define FN_VSYNC     0x800AC734u   /* PsyQ VSync(mode); mode 0 = next frame */
#define FN_SWAP_HOOK 0x8008A2B8u
int ch_turns_poll(CPUState *cpu);
static int g_tf_waiting;
static uint32_t g_tf_gpr[32], g_tf_hi, g_tf_lo;

static void tf_wait_a_frame(CPUState *cpu)
{
    cpu->gpr[4] = 0;                 /* VSync(0) */
    cpu->gpr[31] = FN_SWAP_HOOK;     /* ...then back here */
    cpu->pc = FN_VSYNC;
}

static void tf_resume(CPUState *cpu, int over)
{
    memcpy(cpu->gpr, g_tf_gpr, sizeof g_tf_gpr);
    cpu->hi = g_tf_hi;
    cpu->lo = g_tf_lo;
    cpu->pc = 0;
    g_tf_waiting = 0;
    if (over) {
        fprintf(stderr, "[hotseat] game over\n");
        g_ready = 0;
    }
}

/* Called only from End Turn, after the bases are counted for the win/lose
 * check: the turn is over, swap the sides. */
void func_8008A2B8(CPUState *cpu)
{
    if (g_tf_waiting) {
        /* Back from one frame of VSync while waiting for the opponent. */
        const int r = ch_turns_poll(cpu);
        if (r < 0) {
            tf_wait_a_frame(cpu);
            return;
        }
        tf_resume(cpu, r);
    } else if (cpu->pc == 0 && ch_hotseat_active() && g_ready) {
        /* If either side has no bases left, End Turn goes on to the win or
         * lose screens: no swap, so they are shown for the side that just
         * played. */
        const uint32_t bases = cpu->read_word(PTR_BASE_TABLE);
        int mine = 0, theirs = 0;
        for (int b = 0; b < BASE_COUNT; b++) {
            const uint8_t o = cpu->read_byte(bases + b * BASE_SIZE);
            mine += (o == 1);
            theirs += (o == 2);
        }
        if (ch_turns_active()) {
            /* Two copies: send my half, load the opponent's (turn_files.c). */
            if (g_snapped)
                restore_waiting_side(cpu);
            const int r = ch_turns_swap_point(cpu, !(mine && theirs));
            if (r < 0) {
                memcpy(g_tf_gpr, cpu->gpr, sizeof g_tf_gpr);
                g_tf_hi = cpu->hi;
                g_tf_lo = cpu->lo;
                g_tf_waiting = 1;
                tf_wait_a_frame(cpu);
                return;
            }
            if (r) {
                fprintf(stderr, "[hotseat] game over\n");
                g_ready = 0;
            }
        } else if (mine && theirs) {
            swap_sides(cpu);
        } else {
            if (g_snapped)
                restore_waiting_side(cpu);
            fprintf(stderr, "[hotseat] game over: side %c %s\n", g_side ? 'B' : 'A',
                    theirs ? "has no bases left" : "took the last enemy base");
            g_ready = 0;
        }
    }
    ch_remote_lle_func_8008A2B8(cpu);
}
