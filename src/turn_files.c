/*
 * Turn files: campaign versus between two copies of the game (multiplayer
 * plan step 5; built with -DCH_REMOTE_BATTLE=ON, switched on at run time
 * with CH_TURN_DIR).
 *
 * This builds on hot-seat (hotseat.c). Each player has their own copy and
 * always plays "as the player". Turns alternate strictly: A1, B1, A2, B2...
 * End Turn on a copy runs only that player's half, exactly as in hot-seat
 * (their units move and fight with the opponent's real designs; the CPU
 * brain is skipped; the waiting side is snapshotted and put back). Then, at
 * the point where hot-seat would swap the sides (0x8008A2B8):
 *
 *   1. SEND: the state after my half is written to a turn file, already
 *      turned round to the receiver's point of view (units i <-> i+32,
 *      owner bytes 1 <-> 2 in units, bases and OKEs, the two funds swapped),
 *      together with my design cards, the orders I gave this turn and the
 *      results of the battles I fought.
 *   2. WAIT for the opponent's next turn file to appear. The game keeps
 *      running meanwhile (see "waiting" below), so the window responds.
 *   3. LOAD it: its tables are written straight into RAM (they are already
 *      in my point of view), its design cards become "the other side's
 *      designs" for my next battles, and the turn counters are set.
 *   End Turn then goes on as usual (win/lose check, back to the map).
 *
 * The state in a turn file is authoritative: the receiver does not replay
 * the sender's turn, it takes the result. So the two copies cannot drift
 * apart, and a battle is only ever fought once, on the device of the player
 * whose unit moved (who watches it). The orders and battle results are in
 * the file too, for the "what happened" log now and for a visual replay
 * later.
 *
 * Transport: a folder. Turn N is <CH_TURN_DIR>/turn-NNNNNN.cht. A writes the
 * odd numbers, B the even ones. Two copies on one PC can share the folder;
 * online, turn_relay.c uploads my files to the turn relay and downloads the
 * opponent's into the folder (CH_TURN_RELAY, CH_TURN_GAME, CH_TURN_KEY).
 *
 * Run-time switches:
 *   CH_TURN_DIR=<folder>   the shared folder (turns this mode on; optional
 *                          with a relay, see turn_relay.c)
 *   CH_TURN_ROLE=A|B       which player this copy is (default A)
 *
 * Starting and resuming: when the map opens (0x800333F8, the last step of
 * the map start 0x8003188C, after the tables are set up or loaded from a
 * save) the newest turn file in the folder is loaded straight away, so the
 * player sees the real state before giving any orders:
 *   - newest file is the opponent's: it is my turn, carry on from it;
 *   - newest file is mine: I already played, it is the opponent's turn. My
 *     own file is loaded (turned back to my point of view) and the next End
 *     Turn only waits for theirs;
 *   - no files: player A starts a new game (plays turn 1); player B's first
 *     End Turn waits for A's turn 1.
 * With the relay, the folder is first brought up to date with it (all turn
 * files are fetched on a new computer). A new game is only started once the
 * relay has said it has no turn files, so player A on an empty folder never
 * overwrites a game in progress.
 *
 * A receive-only End Turn holds my units still, sends nothing and loads the
 * opponent's next file. If the map start did not catch up (another
 * scenario, or a build without this hook), the first End Turn still does,
 * as before.
 *
 * After sending, the player may close the game: with the relay, the status
 * line says when the turn file is on it. The launcher also sends any turn
 * file the game could not (launcher/ch_online.cpp).
 *
 * Scenario check: every turn file says which scenario and map it was played
 * on (0x800DF384 and 0x800DF388). When the map opens, the game compares them
 * with the scenario the player actually started. If they differ, or a new
 * online game is started on a scenario that online play does not support
 * yet (only Europa (Practice), CH_ONLINE_SCENARIOS), nothing is loaded or
 * sent: the bottom line says which scenario to start, and online play is
 * off until the game is restarted (the map plays as a normal campaign).
 *
 * File layout (version 2, little-endian), 52-byte header then the body:
 *    0  4  "CHTF"
 *    4  2  version (1)
 *    6  1  sender role (0 = A, 1 = B)
 *    7  1  flags (bit 0: game over after this turn)
 *    8  4  sequence number (A odd, B even, from 1)
 *   12  4  turn counter for the receiver (0x800DF37C)
 *   16  4  second turn counter for the receiver (0x800DF380)
 *   20  8  hash of the previous turn file's tables (0 for turn 1)
 *   28  8  hash of this file's tables
 *   36  8  hash of the whole body
 *   44  4  body length
 *   48  1  scenario (0x800DF384: 0 Europa (Practice), 1 Europa, 2 Ganymede,
 *          3 Callisto)
 *   49  1  map number within the scenario (0x800DF388, from 0)
 *   50  2  0 (spare)
 *   body:  tables  = receiver funds (4), sender funds (4), units 64 x 22,
 *                    bases 16 x 92, OKEs 200 x 12 (receiver's point of view)
 *          sender's design cards, 28 x 1152
 *          order count (1), orders x 8: unit slot, kind (1 formed, 2 new
 *              order, 3 unit gone), task, 0, target cell (2), home cell (2)
 *          battle count (1), battles x 42: side-1 unit, side-2 unit (both
 *              in the sender's numbering), x (2), y (2), then for the 6 OKEs
 *              (side 1 then side 2): OKE index, HP, kills (2 each; -1 index
 *              for an empty slot)
 *          version 3 adds, for the replay (turn_replay.c):
 *          battle seeds, 4 x battle count: the random seed each battle was
 *              fought with
 *          start tables (same layout as the tables above, receiver's point
 *              of view): the state when the sender pressed End Turn, after
 *              their orders and before any unit moved
 * Version 2 files are still read (no replay).
 *
 * Replay ("watch what the enemy did", version 3): when the opponent's turn
 * file arrives, the game first puts the start tables in RAM and runs the
 * game's own unit step (0x80034C38, 0x8001BCB0(2), 0x8001C000) for each of
 * their units, as End Turn does, so their units walk on the map. A battle on
 * the way is fought again with the sender's seed, with RAM turned round to
 * the sender's point of view (so it starts exactly as it did for them); the
 * recorded result is then written over whatever the replay gave. Then the
 * turn file's end state is loaded as before, so the replay can never change
 * the game. After each of their units steps, the replay waits half a second
 * (CH_TURN_REPLAY_PAUSE frames, default 30). CH_TURN_REPLAY=0 turns it off.
 *
 * The previous-tables hash links the files into a chain: each file says
 * which state it was played from, and the receiver checks that it is the
 * state it sent. A mismatch is logged as a desync, and the new state is
 * still loaded (it is authoritative), which puts both copies back in step.
 */
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "cpu_state.h"

#define GP_TURN         0x800DF37Cu
#define GP_TURN2        0x800DF380u
#define GP_SCENARIO     0x800DF384u   /* 0 Europa (Practice), 1 Europa, 2 Ganymede, 3 Callisto */
#define GP_MAP          0x800DF388u   /* map number within the scenario, from 0 */
#define GP_FUNDS_PLAYER 0x800DF38Cu
#define GP_FUNDS_CPU    0x800DF390u
#define GP_CUR_BASE     0x800DF404u
#define PTR_OKE_TABLE   0x800E014Cu
#define PTR_BASE_TABLE  0x800E0154u
#define PTR_UNIT_TABLE  0x800E017Cu
#define CARD_SIZE       1152u
#define CARD_SLOTS      28
#define OKE_SIZE        12u
#define OKE_COUNT       200
#define BASE_SIZE       92u
#define BASE_COUNT      16
#define UNIT_SIZE       22u
#define UNIT_COUNT      64

#define TF_VERSION      3
#define TF_MIN_VERSION  2         /* oldest version still read (no replay) */
#define TF_HEADER       52u

/* Scenarios online play supports, one bit per scenario number. For now only
 * Europa (Practice). */
#ifndef CH_ONLINE_SCENARIOS
#define CH_ONLINE_SCENARIOS 0x1u
#endif
#define TF_TABLES       (8u + UNIT_COUNT * UNIT_SIZE + BASE_COUNT * BASE_SIZE + OKE_COUNT * OKE_SIZE)
#define TF_CARDS        (CARD_SLOTS * CARD_SIZE)
#define TF_ORDER_SIZE   8u
#define TF_BATTLE_SIZE  42u
#define TF_MAX_ORDERS   64
#define TF_MAX_BATTLES  64
#define TF_MAX_BODY     (TF_TABLES + TF_CARDS + 1u + TF_MAX_ORDERS * TF_ORDER_SIZE + \
                         1u + TF_MAX_BATTLES * TF_BATTLE_SIZE + \
                         TF_MAX_BATTLES * 4u + TF_TABLES)

/* turn_relay.c: keeps the folder in step with the online relay. */
int ch_relay_start(int role, const char *dir, char *dir_out, size_t dir_n);
void ch_relay_want(uint32_t seq);
int ch_relay_ready(void);
int ch_relay_wait_ready(int ms);
uint32_t ch_relay_uploaded(void);
static int g_relay;

/* From hotseat.c: the waiting side's design cards (28 x 1152, flat). */
uint8_t *ch_hotseat_other_cards(void);
void ch_hotseat_setup_now(CPUState *cpu);
void ch_hotseat_off(void);
void ch_remote_battle_off(void);
extern void host_osd_set_status(const char *msg);

static int g_on = -1;
static char g_dir[512];
static int g_role;                /* 0 = A, 1 = B */
static int g_synced;              /* a turn file has been loaded (or A sent turn 1) */
static int g_receive_only;        /* this End Turn only receives */
static uint32_t g_next_send;      /* sequence number of my next turn file */
static uint64_t g_prev_hash;      /* tables hash of the last file loaded */
static uint32_t g_sent;           /* the last turn file I sent this session */
static int g_over;                /* the newest turn file ended the game */
static int g_scen = -1, g_map = -1; /* the scenario and map this copy is playing */

/* My units at the start of my turn, for working out the orders I gave. */
static uint8_t g_start_units[32][UNIT_SIZE];
static int g_have_start;
static uint8_t g_held_owner[32];  /* my units, held still in a receive-only End Turn */
static int g_held;

static uint8_t g_orders[TF_MAX_ORDERS][TF_ORDER_SIZE];
static int g_norders;
static uint8_t g_battles[TF_MAX_BATTLES][TF_BATTLE_SIZE];
static int g_nbattles;
static uint32_t g_battle_seed[TF_MAX_BATTLES];
static uint32_t g_cur_seed;       /* seed of the battle being fought */

/* The tables when I pressed End Turn (after my orders, before any unit
 * moved), as they are in RAM: player funds, CPU funds, units, bases, OKEs.
 * Sent in the turn file for the opponent's replay. */
static uint8_t g_start_raw[TF_TABLES];
static int g_have_start_raw;

static uint8_t g_buf[TF_HEADER + TF_MAX_BODY];

int ch_turns_active(void)
{
    if (g_on < 0) {
        const char *d = getenv("CH_TURN_DIR");
        const char *r = getenv("CH_TURN_ROLE");
        g_role = (r && (*r == 'B' || *r == 'b' || *r == '2')) ? 1 : 0;
        /* Online (turn_relay.c): the relay fills a local folder. */
        g_relay = ch_relay_start(g_role, d, g_dir, sizeof g_dir);
        g_on = g_relay || (d && *d);
        if (g_on) {
            if (!g_relay) snprintf(g_dir, sizeof g_dir, "%s", d);
            fprintf(stderr, "[turns] on: player %c, folder %s\n", g_role ? 'B' : 'A', g_dir);
        }
    }
    return g_on;
}

/* On, without switching it on: for rand() (turn_replay.c), which the game
 * calls long before the map opens. */
int ch_turns_on(void) { return g_on > 0; }

/* ---- little helpers ---------------------------------------------------- */

static uint64_t fnv1a(const uint8_t *p, size_t n)
{
    uint64_t h = 0xcbf29ce484222325ull;
    for (size_t i = 0; i < n; i++) { h ^= p[i]; h *= 0x100000001b3ull; }
    return h;
}

static void put16(uint8_t *p, uint16_t v) { p[0] = (uint8_t)v; p[1] = (uint8_t)(v >> 8); }
static void put32(uint8_t *p, uint32_t v) { put16(p, (uint16_t)v); put16(p + 2, (uint16_t)(v >> 16)); }
static void put64(uint8_t *p, uint64_t v) { put32(p, (uint32_t)v); put32(p + 4, (uint32_t)(v >> 32)); }
static uint16_t get16(const uint8_t *p) { return (uint16_t)(p[0] | (p[1] << 8)); }
static uint32_t get32(const uint8_t *p) { return get16(p) | ((uint32_t)get16(p + 2) << 16); }
static uint64_t get64(const uint8_t *p) { return get32(p) | ((uint64_t)get32(p + 4) << 32); }

static void rd(CPUState *cpu, uint32_t a, uint8_t *d, uint32_t n)
{ for (uint32_t i = 0; i < n; i++) d[i] = cpu->read_byte(a + i); }
static void wr(CPUState *cpu, uint32_t a, const uint8_t *s, uint32_t n)
{ for (uint32_t i = 0; i < n; i++) cpu->write_byte(a + i, s[i]); }

static uint8_t swap_owner(uint8_t o) { return o == 1 ? 2 : o == 2 ? 1 : o; }

/* The campaign tables as they are in RAM: player funds, CPU funds, units,
 * bases, OKEs (TF_TABLES bytes). */
static void read_raw_tables(CPUState *cpu, uint8_t *raw)
{
    put32(raw, cpu->read_word(GP_FUNDS_PLAYER));
    put32(raw + 4, cpu->read_word(GP_FUNDS_CPU));
    raw += 8;
    rd(cpu, cpu->read_word(PTR_UNIT_TABLE), raw, UNIT_COUNT * UNIT_SIZE); raw += UNIT_COUNT * UNIT_SIZE;
    rd(cpu, cpu->read_word(PTR_BASE_TABLE), raw, BASE_COUNT * BASE_SIZE); raw += BASE_COUNT * BASE_SIZE;
    rd(cpu, cpu->read_word(PTR_OKE_TABLE), raw, OKE_COUNT * OKE_SIZE);
}

/* Raw tables turned round to the receiver's point of view (the turn-file
 * layout): receiver's funds, sender's funds, units i <-> i+32, owner bytes
 * 1 <-> 2. */
static void tables_for_receiver(uint8_t *p, const uint8_t *raw)
{
    memcpy(p, raw + 4, 4);
    memcpy(p + 4, raw, 4);
    p += 8; raw += 8;
    for (int i = 0; i < UNIT_COUNT; i++, p += UNIT_SIZE) {
        memcpy(p, raw + ((i + 32) % UNIT_COUNT) * UNIT_SIZE, UNIT_SIZE);
        p[0] = swap_owner(p[0]);
    }
    raw += UNIT_COUNT * UNIT_SIZE;
    for (int i = 0; i < BASE_COUNT + OKE_COUNT; i++) {
        const uint32_t sz = i < BASE_COUNT ? BASE_SIZE : OKE_SIZE;
        memcpy(p, raw, sz);
        p[0] = swap_owner(p[0]);
        p += sz; raw += sz;
    }
}

/* Turn-file tables (already in my point of view) into RAM. */
static void write_tables(CPUState *cpu, const uint8_t *p)
{
    cpu->write_word(GP_FUNDS_PLAYER, get32(p));
    cpu->write_word(GP_FUNDS_CPU, get32(p + 4));
    p += 8;
    wr(cpu, cpu->read_word(PTR_UNIT_TABLE), p, UNIT_COUNT * UNIT_SIZE); p += UNIT_COUNT * UNIT_SIZE;
    wr(cpu, cpu->read_word(PTR_BASE_TABLE), p, BASE_COUNT * BASE_SIZE); p += BASE_COUNT * BASE_SIZE;
    wr(cpu, cpu->read_word(PTR_OKE_TABLE), p, OKE_COUNT * OKE_SIZE);
}

/* "Europa (Practice)", "Ganymede map 2"... (scenarios with one map get no
 * map number). */
static const char *scenario_name(int scen, int map)
{
    static const char *names[] = { "Europa (Practice)", "Europa", "Ganymede", "Callisto" };
    static char buf[2][48];
    static int which;
    char *b = buf[which ^= 1];
    const char *n = scen >= 0 && scen < 4 ? names[scen] : "an unknown scenario";
    if (scen == 0 || map <= 0) snprintf(b, sizeof buf[0], "%s", n);
    else snprintf(b, sizeof buf[0], "%s map %d", n, map + 1);
    return b;
}

static void path_for(char *out, size_t n, uint32_t seq)
{
    snprintf(out, n, "%s/turn-%06u.cht", g_dir, (unsigned)seq);
}

static int seq_exists(uint32_t seq)
{
    char p[600];
    path_for(p, sizeof p, seq);
    FILE *f = fopen(p, "rb");
    if (f) fclose(f);
    return f != NULL;
}

static int seq_is_mine(uint32_t seq) { return (int)((seq & 1u) == 0) == g_role; }

/* Turn files are numbered 1, 2, 3... with no gaps. */
static uint32_t latest_seq(int mine_only)
{
    uint32_t best = 0;
    for (uint32_t s = 1; seq_exists(s); s++)
        if (!mine_only || seq_is_mine(s)) best = s;
    return best;
}

/* ---- orders and battles ------------------------------------------------ */

static void capture_start(CPUState *cpu)
{
    const uint32_t units = cpu->read_word(PTR_UNIT_TABLE);
    for (int i = 0; i < 32; i++)
        rd(cpu, units + i * UNIT_SIZE, g_start_units[i], UNIT_SIZE);
    g_have_start = 1;
}

/* Compare my units with how they were at the start of my turn. Every order
 * screen writes only task (+01), target (+06) and home (+08), and Form Unit
 * fills an empty slot, so the differences are the orders. */
static void capture_orders(CPUState *cpu)
{
    const uint32_t units = cpu->read_word(PTR_UNIT_TABLE);
    g_norders = 0;
    for (int i = 0; i < 32 && g_norders < TF_MAX_ORDERS; i++) {
        uint8_t u[UNIT_SIZE];
        rd(cpu, units + i * UNIT_SIZE, u, UNIT_SIZE);
        const uint8_t *s = g_start_units[i];
        const int was = g_have_start && s[0] == 1, now = u[0] == 1;
        int kind = 0;
        if (now && !was) kind = 1;
        else if (now && (u[1] != s[1] || get16(u + 6) != get16(s + 6) || get16(u + 8) != get16(s + 8)))
            kind = 2;
        else if (!now && was) kind = 3;
        if (!kind) continue;
        uint8_t *o = g_orders[g_norders++];
        o[0] = (uint8_t)i; o[1] = (uint8_t)kind; o[2] = u[1]; o[3] = 0;
        put16(o + 4, get16(u + 6));
        put16(o + 6, get16(u + 8));
    }
}

/* Called by remote_battle.c when a battle is over, before the game's result
 * code removes destroyed OKEs. unit1 is the moving player's side (side 1). */
void ch_turns_note_battle(CPUState *cpu, uint32_t unit1, uint32_t unit2)
{
    if (!ch_turns_active() || g_nbattles >= TF_MAX_BATTLES)
        return;
    const uint32_t units = cpu->read_word(PTR_UNIT_TABLE);
    const uint32_t okes  = cpu->read_word(PTR_OKE_TABLE);
    g_battle_seed[g_nbattles] = g_cur_seed;
    uint8_t *b = g_battles[g_nbattles++];
    const uint32_t u2 = units + unit2 * UNIT_SIZE;
    b[0] = (uint8_t)unit1;
    b[1] = (uint8_t)unit2;
    put16(b + 2, cpu->read_half(u2 + 2));
    put16(b + 4, cpu->read_half(u2 + 4));
    for (int side = 0; side < 2; side++) {
        const uint32_t u = units + (side ? unit2 : unit1) * UNIT_SIZE;
        for (int s = 0; s < 3; s++) {
            uint8_t *e = b + 6 + (side * 3 + s) * 6;
            const int16_t k = (int16_t)cpu->read_half(u + 0x0C + 2 * s);
            put16(e, (uint16_t)k);
            put16(e + 2, k < 0 ? 0 : cpu->read_half(okes + k * OKE_SIZE + 6));
            put16(e + 4, k < 0 ? 0 : cpu->read_half(okes + k * OKE_SIZE + 4));
        }
    }
}

/* turn_replay.c: the game's rand() while online. */
uint32_t ch_rand_get(void);
void ch_rand_set(uint32_t seed);

/* Called by remote_battle.c when a battle starts on the sender's device:
 * give it a fresh seed, so the opponent's replay can fight it again the
 * same way. */
void ch_turns_battle_starts(void)
{
    if (!ch_turns_active())
        return;
    g_cur_seed = (uint32_t)fnv1a((const uint8_t *)&g_next_send, sizeof g_next_send) ^
                 (ch_rand_get() * 2654435761u) ^ ((uint32_t)g_nbattles << 24);
    ch_rand_set(g_cur_seed);
}

/* ---- End Turn hooks (called from hotseat.c) ---------------------------- */

/* Start of End Turn (fresh call, after hot-seat set-up). */
void ch_turns_begin(CPUState *cpu)
{
    host_osd_set_status("");
    if (!g_synced) {
        if (g_role == 0 && latest_seq(0) == 0 && (!g_relay || ch_relay_ready() == 1)) {
            g_receive_only = 0;     /* new game: A plays and sends turn 1 */
            g_next_send = 1;
            g_prev_hash = 0;
            fprintf(stderr, "[turns] new game: player A plays turn 1\n");
        } else {
            g_receive_only = 1;
        }
    }
    g_nbattles = 0;
    g_norders = 0;
    if (g_receive_only) {
        /* Hold my units still: the units loop skips owner 0. Put back (or
         * overwritten by the loaded state) at the swap point. */
        const uint32_t units = cpu->read_word(PTR_UNIT_TABLE);
        for (int i = 0; i < 32; i++) {
            g_held_owner[i] = cpu->read_byte(units + i * UNIT_SIZE);
            cpu->write_byte(units + i * UNIT_SIZE, 0);
        }
        g_held = 1;
        fprintf(stderr, "[turns] this End Turn only receives the opponent's turn\n");
    } else {
        capture_orders(cpu);
        read_raw_tables(cpu, g_start_raw);
        g_have_start_raw = 1;
    }
}

static size_t build_file(CPUState *cpu, int game_over)
{
    static uint8_t raw[TF_TABLES];
    uint8_t *h = g_buf, *body = g_buf + TF_HEADER, *p = body;

    /* Tables, turned round to the receiver's point of view. */
    read_raw_tables(cpu, raw);
    tables_for_receiver(p, raw);
    p += TF_TABLES;
    const uint64_t tables_hash = fnv1a(body, TF_TABLES);

    /* My designs: the receiver fights my OKEs with them. */
    const uint32_t cards = cpu->read_word(0x800E0014u);
    rd(cpu, cards, p, TF_CARDS); p += TF_CARDS;

    *p++ = (uint8_t)g_norders;
    for (int i = 0; i < g_norders; i++, p += TF_ORDER_SIZE) memcpy(p, g_orders[i], TF_ORDER_SIZE);
    *p++ = (uint8_t)g_nbattles;
    for (int i = 0; i < g_nbattles; i++, p += TF_BATTLE_SIZE) memcpy(p, g_battles[i], TF_BATTLE_SIZE);

    /* Version 3: for the replay. Without a start snapshot (cannot happen
     * for a turn I played) the end state stands in, and nothing moves. */
    for (int i = 0; i < g_nbattles; i++, p += 4) put32(p, g_battle_seed[i]);
    if (g_have_start_raw) tables_for_receiver(p, g_start_raw);
    else memcpy(p, body, TF_TABLES);
    p += TF_TABLES;
    g_have_start_raw = 0;
    const uint32_t body_len = (uint32_t)(p - body);

    /* The game counted the turn at the start of End Turn. A's turn n and B's
     * turn n make one round: B plays the same number A just played, and A
     * then plays the next one. */
    const uint32_t back = g_role == 0 ? 1u : 0u;
    memcpy(h, "CHTF", 4);
    put16(h + 4, TF_VERSION);
    h[6] = (uint8_t)g_role;
    h[7] = (uint8_t)(game_over ? 1 : 0);
    put32(h + 8, g_next_send);
    put32(h + 12, cpu->read_word(GP_TURN) - back);
    put32(h + 16, cpu->read_word(GP_TURN2) - back);
    put64(h + 20, g_prev_hash);
    put64(h + 28, tables_hash);
    put64(h + 36, fnv1a(body, body_len));
    put32(h + 44, body_len);
    h[48] = (uint8_t)cpu->read_word(GP_SCENARIO);
    h[49] = (uint8_t)cpu->read_word(GP_MAP);
    h[50] = h[51] = 0;
    return TF_HEADER + body_len;
}

static int send_file(CPUState *cpu, int game_over)
{
    char path[600], tmp[620];
    const size_t n = build_file(cpu, game_over);
    path_for(path, sizeof path, g_next_send);
    snprintf(tmp, sizeof tmp, "%s.part", path);
    FILE *f = fopen(tmp, "wb");
    if (!f || fwrite(g_buf, 1, n, f) != n) {
        if (f) fclose(f);
        fprintf(stderr, "[turns] ERROR: could not write %s\n", tmp);
        return 0;
    }
    fclose(f);
    remove(path);                 /* rename does not replace on Windows */
    if (rename(tmp, path) != 0) {
        fprintf(stderr, "[turns] ERROR: could not rename %s to %s\n", tmp, path);
        return 0;
    }
    g_sent = g_next_send;
    fprintf(stderr, "[turns] sent turn file %u (%u order(s), %u battle(s)%s), %zu bytes\n",
            (unsigned)g_next_send, (unsigned)g_norders, (unsigned)g_nbattles,
            game_over ? ", game over" : "", n);
    return 1;
}

/* Read and check turn file seq into g_buf. Returns the body length, or 0. */
static uint32_t read_file(uint32_t seq)
{
    char path[600];
    path_for(path, sizeof path, seq);
    FILE *f = fopen(path, "rb");
    if (!f) return 0;
    const size_t n = fread(g_buf, 1, sizeof g_buf, f);
    fclose(f);
    if (n < 8 || memcmp(g_buf, "CHTF", 4) != 0) {
        fprintf(stderr, "[turns] ERROR: %s is not a turn file\n", path);
        return 0;
    }
    const unsigned ver = get16(g_buf + 4);
    if (ver < TF_MIN_VERSION || ver > TF_VERSION || n < TF_HEADER) {
        fprintf(stderr, "[turns] ERROR: %s is a version-%u turn file; this game reads versions %d-%d "
                "(%s)\n", path, ver, TF_MIN_VERSION, TF_VERSION,
                ver > TF_VERSION ? "update the game" : "start a new online game");
        return 0;
    }
    const uint32_t len = get32(g_buf + 44);
    if (get32(g_buf + 8) != seq || len < TF_TABLES + TF_CARDS + 2u || n != TF_HEADER + len ||
        fnv1a(g_buf + TF_HEADER, len) != get64(g_buf + 36)) {
        fprintf(stderr, "[turns] ERROR: %s is damaged or incomplete\n", path);
        return 0;
    }
    if (g_buf[6] != (seq & 1u ? 0 : 1)) {
        fprintf(stderr, "[turns] ERROR: %s was written by the wrong player\n", path);
        return 0;
    }
    if (g_scen >= 0 && (g_buf[48] != g_scen || g_buf[49] != g_map)) {
        fprintf(stderr, "[turns] ERROR: %s was played on %s, but this game is on %s\n", path,
                scenario_name(g_buf[48], g_buf[49]), scenario_name(g_scen, g_map));
        return 0;
    }
    return len;
}

/* Tables hash of an earlier turn file (0 if it is not there). */
static uint64_t tables_hash_of(uint32_t seq)
{
    if (seq == 0) return 0;
    char path[600];
    uint8_t h[TF_HEADER];
    path_for(path, sizeof path, seq);
    FILE *f = fopen(path, "rb");
    if (!f) return 0;
    const size_t n = fread(h, 1, TF_HEADER, f);
    fclose(f);
    return n == TF_HEADER ? get64(h + 28) : 0;
}

static void log_what_happened(const uint8_t *orders, int norders, const uint8_t *battles, int nbattles)
{
    static const char *task[] = { "standby", "move", "defend a base", "capture a base", "patrol" };
    for (int i = 0; i < norders; i++) {
        const uint8_t *o = orders + i * TF_ORDER_SIZE;
        const unsigned their = o[0] + 32u;   /* their slot i is my i + 32 */
        if (o[1] == 3) { fprintf(stderr, "[turns]  their unit %u: gone\n", their); continue; }
        fprintf(stderr, "[turns]  their unit %u: %s, %s, target cell %u\n", their,
                o[1] == 1 ? "formed" : "new order", o[2] < 5 ? task[o[2]] : "?", get16(o + 4));
    }
    for (int i = 0; i < nbattles; i++) {
        const uint8_t *b = battles + i * TF_BATTLE_SIZE;
        fprintf(stderr, "[turns]  battle at (%d,%d): their unit %u attacked your unit %u.",
                (int16_t)get16(b + 2), (int16_t)get16(b + 4), b[0] + 32u, (unsigned)b[1] - 32u);
        for (int side = 0; side < 2; side++) {
            fprintf(stderr, side ? " Yours:" : " Theirs:");
            for (int s = 0; s < 3; s++) {
                const uint8_t *e = b + 6 + (side * 3 + s) * 6;
                if ((int16_t)get16(e) < 0) continue;
                fprintf(stderr, " %dHP", (int16_t)get16(e + 2));
            }
        }
        fprintf(stderr, "\n");
    }
}

/* Write a checked turn file (in g_buf) into RAM. Returns its game-over flag. */
static int load_file(CPUState *cpu, uint32_t seq, uint32_t len)
{
    const uint32_t bases = cpu->read_word(PTR_BASE_TABLE);
    const uint8_t *h = g_buf, *p = g_buf + TF_HEADER, *end = p + len;

    /* Chain check: was it played from the state I sent? */
    const uint64_t mine = tables_hash_of(seq - 1);
    if (seq > 1 && mine && get64(h + 20) != mine)
        fprintf(stderr, "[turns] DESYNC: turn file %u was not played from turn file %u; "
                "loading it anyway (it is the opponent's real state)\n", (unsigned)seq, (unsigned)(seq - 1));
    g_prev_hash = get64(h + 28);

    write_tables(cpu, p); p += TF_TABLES;
    memcpy(ch_hotseat_other_cards(), p, TF_CARDS); p += TF_CARDS;
    cpu->write_word(GP_TURN, get32(h + 12));
    cpu->write_word(GP_TURN2, get32(h + 16));

    /* Keep the selected base on one of mine. */
    const uint32_t cur = cpu->read_word(GP_CUR_BASE);
    if (cur >= BASE_COUNT || cpu->read_byte(bases + cur * BASE_SIZE) != 1)
        for (uint32_t b = 0; b < BASE_COUNT; b++)
            if (cpu->read_byte(bases + b * BASE_SIZE) == 1) { cpu->write_word(GP_CUR_BASE, b); break; }

    int norders = 0, nbattles = 0;
    const uint8_t *orders = NULL, *battles = NULL;
    if (p < end) { norders = *p++; orders = p; p += norders * TF_ORDER_SIZE; }
    if (p < end) { nbattles = *p++; battles = p; p += nbattles * TF_BATTLE_SIZE; }
    if (p > end) norders = nbattles = 0;
    fprintf(stderr, "[turns] loaded turn file %u: turn %u, funds %u (opponent %u)%s\n",
            (unsigned)seq, cpu->read_word(GP_TURN), cpu->read_word(GP_FUNDS_PLAYER),
            cpu->read_word(GP_FUNDS_CPU), (h[7] & 1) ? ", game over" : "");
    log_what_happened(orders, norders, battles, nbattles);
    return h[7] & 1;
}

/* ---- replay: watch what the opponent did -------------------------------
 *
 * Runs from a hook (the swap point, or the map start) that the game "calls
 * back" after each step: replay_step() sets up a call to one of the game's
 * own unit routines with the hook as the return address, exactly as the
 * waiting loop calls VSync. End Turn's unit loop (0x8001B8EC) is, per unit
 * with a nonzero owner byte: 0x80034C38(unit) (order/path step),
 * 0x8001BCB0(2), 0x8001C000(unit) (move one hex; may start a battle). The
 * replay runs it for the opponent's units, 32-63, from the start tables. */

#define FN_TURN_PREP    0x800341FCu   /* End Turn's first step: rebuilds the hex table from the units */
#define FN_UNIT_PATH    0x80034C38u
#define FN_UNIT_SHOW    0x8001BCB0u
#define FN_UNIT_MOVE    0x8001C000u

static int g_replay_on = -1;      /* CH_TURN_REPLAY (default on) */
static uint8_t g_rp_buf[TF_HEADER + TF_MAX_BODY];
static uint32_t g_rp_len;
static int g_rp_active;
static int g_rp_unit, g_rp_step, g_rp_wait, g_rp_prep;
static int g_rp_pause = -1;       /* frames to wait after each of their units steps */
#define FN_VSYNC_REPLAY 0x800AC734u   /* PsyQ VSync(0): one frame */
static uint32_t g_rp_ret;         /* the hook the game returns to */
static const uint8_t *g_rp_battles, *g_rp_seeds;
static int g_rp_nbattles;
static uint8_t g_rp_used[TF_MAX_BATTLES];
static int g_rp_fought, g_rp_matched;

static int replay_enabled(void)
{
    if (g_replay_on < 0) {
        const char *e = getenv("CH_TURN_REPLAY");
        g_replay_on = !(e && *e == '0');
        e = getenv("CH_TURN_REPLAY_PAUSE");
        g_rp_pause = e ? atoi(e) : 30;
        if (g_rp_pause < 0 || g_rp_pause > 600) g_rp_pause = 30;
    }
    return g_replay_on;
}

int ch_turns_replaying(void) { return g_rp_active; }

/* The version-3 parts of a turn file in buf: the battle records, their
 * seeds and the start tables. NULL for version 2 or a bad layout. */
static const uint8_t *replay_parts(const uint8_t *buf, uint32_t len, const uint8_t **battles,
                                   const uint8_t **seeds, int *nbattles)
{
    if (get16(buf + 4) < 3) return NULL;
    const uint8_t *p = buf + TF_HEADER + TF_TABLES + TF_CARDS, *end = buf + TF_HEADER + len;
    if (p >= end) return NULL;
    const int norders = *p++;
    p += norders * TF_ORDER_SIZE;
    if (p >= end) return NULL;
    *nbattles = *p++;
    *battles = p;
    p += *nbattles * TF_BATTLE_SIZE;
    *seeds = p;
    p += *nbattles * 4;
    if (p + TF_TABLES != end) return NULL;
    return p;
}

/* The opponent's turn file seq (checked, in g_buf) has arrived: start the
 * replay. Returns 0 if there is nothing to replay. */
static int replay_begin(CPUState *cpu, uint32_t len, uint32_t ret)
{
    if (!replay_enabled()) return 0;
    const uint8_t *battles, *seeds;
    int nbattles;
    const uint8_t *start = replay_parts(g_buf, len, &battles, &seeds, &nbattles);
    if (!start) return 0;
    /* Anything to watch? Their units in the start tables (slots 32-63). */
    int moving = 0;
    for (int i = 32; i < UNIT_COUNT; i++)
        moving += start[8 + i * UNIT_SIZE] != 0;
    if (!moving) return 0;

    memcpy(g_rp_buf, g_buf, TF_HEADER + len);
    g_rp_len = len;
    const ptrdiff_t off = start - g_buf;
    g_rp_battles = g_rp_buf + (battles - g_buf);
    g_rp_seeds = g_rp_buf + (seeds - g_buf);
    g_rp_nbattles = nbattles;
    memset(g_rp_used, 0, sizeof g_rp_used);
    g_rp_fought = g_rp_matched = 0;

    write_tables(cpu, g_rp_buf + off);
    /* Their designs, for the battles (remote_battle.c turns RAM round to
     * their point of view while a battle is fought again). */
    memcpy(ch_hotseat_other_cards(), g_rp_buf + TF_HEADER + TF_TABLES, TF_CARDS);
    g_rp_unit = 32;
    g_rp_step = 0;
    g_rp_wait = g_rp_pause;     /* a moment to see the start position */
    g_rp_prep = 1;
    g_rp_ret = ret;
    g_rp_active = 1;

    char msg[128];
    snprintf(msg, sizeof msg, "Player %c's turn: watching what they did...", g_role ? 'A' : 'B');
    host_osd_set_status(msg);
    fprintf(stderr, "[turns] replay of turn file %u: %d unit(s), %d battle(s)\n",
            (unsigned)get32(g_rp_buf + 8), moving, nbattles);
    return 1;
}

/* Set up the next call of the game's unit routines. Returns 1 if a call is
 * set up (the hook must just return), 0 when the replay is over (g_buf
 * holds the turn file again, to be loaded as usual). */
static int replay_step(CPUState *cpu)
{
    const uint32_t units = cpu->read_word(PTR_UNIT_TABLE);
    if (g_rp_wait > 0) {
        /* A short pause after a unit's step, so the player sees it. */
        g_rp_wait--;
        cpu->gpr[4] = 0;
        cpu->gpr[31] = g_rp_ret;
        cpu->pc = FN_VSYNC_REPLAY;
        return 1;
    }
    if (g_rp_prep) {
        /* As End Turn starts: without it a unit walking onto an enemy unit
         * finds no one there when the map has just opened. */
        g_rp_prep = 0;
        cpu->gpr[31] = g_rp_ret;
        cpu->pc = FN_TURN_PREP;
        return 1;
    }
    while (g_rp_unit < UNIT_COUNT) {
        if (g_rp_step == 0 && cpu->read_byte(units + g_rp_unit * UNIT_SIZE) == 0) {
            g_rp_unit++;
            continue;
        }
        static const uint32_t fn[3] = { FN_UNIT_PATH, FN_UNIT_SHOW, FN_UNIT_MOVE };
        cpu->gpr[4] = g_rp_step == 1 ? 2u : (uint32_t)g_rp_unit;
        cpu->gpr[31] = g_rp_ret;
        cpu->pc = fn[g_rp_step];
        if (++g_rp_step == 3) {
            g_rp_step = 0;
            g_rp_unit++;
            g_rp_wait = g_rp_pause;
        }
        return 1;
    }
    g_rp_active = 0;
    /* How close the replay came to the sender's end state: each unit's
     * owner, task and position. (OKE HP is checked per battle; base repair,
     * production and resupply ran after the moves on their device, so the
     * rest of the tables differ.) */
    int differ = 0;
    const uint8_t *end = g_rp_buf + TF_HEADER + 8;
    for (int i = 0; i < UNIT_COUNT; i++) {
        uint8_t u[6];
        rd(cpu, units + i * UNIT_SIZE, u, 6);
        const uint8_t *e = end + i * UNIT_SIZE;
        if ((u[0] != 0) != (e[0] != 0) || (e[0] && memcmp(u, e, 6) != 0)) differ++;
    }
    memcpy(g_buf, g_rp_buf, TF_HEADER + g_rp_len);
    fprintf(stderr, "[turns] replay over: %d battle(s) fought again, %d gave the recorded result; "
            "%d unit(s) differ from the turn file%s\n", g_rp_fought, g_rp_matched, differ,
            differ ? " (the turn file is loaded now)" : "");
    return 0;
}

/* remote_battle.c, at the start of a battle during the replay: my unit
 * (side 1 on my screen) against their moving unit. Returns their battle
 * record (in the sender's numbering) and its seed, or NULL. */
const uint8_t *ch_turns_replay_battle(uint32_t mine, uint32_t theirs, uint32_t *seed)
{
    for (int i = 0; i < g_rp_nbattles; i++) {
        const uint8_t *b = g_rp_battles + i * TF_BATTLE_SIZE;
        if (g_rp_used[i] || b[0] + 32u != theirs || b[1] - 32u != mine) continue;
        g_rp_used[i] = 1;
        *seed = get32(g_rp_seeds + 4 * i);
        g_rp_fought++;
        return b;
    }
    fprintf(stderr, "[turns] replay: no recorded battle for their unit %u against my unit %u\n",
            (unsigned)theirs, (unsigned)mine);
    return NULL;
}

/* remote_battle.c, after a replayed battle (RAM still in the sender's point
 * of view): write the recorded HP and kills over what the replay gave. */
void ch_turns_replay_result(CPUState *cpu, const uint8_t *b)
{
    const uint32_t okes = cpu->read_word(PTR_OKE_TABLE);
    int same = 1;
    for (int e = 0; e < 6; e++) {
        const uint8_t *r = b + 6 + e * 6;
        const int16_t k = (int16_t)get16(r);
        if (k < 0 || k >= OKE_COUNT) continue;
        const uint32_t o = okes + k * OKE_SIZE;
        const int16_t hp = (int16_t)cpu->read_half(o + 6), kills = (int16_t)cpu->read_half(o + 4);
        if (hp != (int16_t)get16(r + 2) || kills != (int16_t)get16(r + 4)) {
            fprintf(stderr, "[turns] replay: OKE %d ended with %dHP %d kill(s), recorded %dHP %d\n",
                    k, hp, kills, (int16_t)get16(r + 2), (int16_t)get16(r + 4));
            same = 0;
        }
        cpu->write_half(o + 6, get16(r + 2));
        cpu->write_half(o + 4, get16(r + 4));
    }
    g_rp_matched += same;
    fprintf(stderr, "[turns] replay: battle at (%d,%d) %s\n", (int16_t)get16(b + 2),
            (int16_t)get16(b + 4), same ? "went exactly as recorded" : "differed; recorded result used");
}

/* ---- waiting for the opponent's file ----------------------------------
 *
 * The game is not paused while it waits. hotseat.c's swap-point hook calls
 * ch_turns_swap_point() once; if the opponent's file is not there yet, it
 * keeps the game running by calling the game's own VSync (one frame) in a
 * loop and asks ch_turns_poll() after each one, until the file arrives. So
 * the window keeps responding (and the debug server keeps answering), and
 * the game shows a "waiting" line on screen meanwhile. */

static uint32_t g_want;           /* the turn file I am waiting for */
static uint32_t g_polls;

static char g_status[128];

static void show_status(const char *msg)
{
    if (strcmp(msg, g_status) == 0) return;
    snprintf(g_status, sizeof g_status, "%s", msg);
    host_osd_set_status(msg);
}

/* The bottom line while waiting: whose turn it is and, online, whether my
 * turn has reached the relay (then the game can be closed). */
static void set_waiting_status(int on)
{
    char msg[128];
    const char other = g_role ? 'A' : 'B';
    if (!on) {
        show_status("");
    } else if (g_want == 0) {
        show_status("Can't reach the relay yet. Trying again...");
    } else if (g_relay && g_sent && ch_relay_uploaded() < g_sent) {
        snprintf(msg, sizeof msg, "Sending your turn to the relay... Waiting for player %c.", other);
        show_status(msg);
    } else if (g_relay && g_sent) {
        snprintf(msg, sizeof msg, "Your turn is sent. You can close the game, or wait for player %c.", other);
        show_status(msg);
    } else {
        snprintf(msg, sizeof msg, "Waiting for player %c's turn (turn file %u)...", other, (unsigned)g_want);
        show_status(msg);
    }
}

static int finish_load(CPUState *cpu, uint32_t len)
{
    const int over = load_file(cpu, g_want, len);
    set_waiting_status(0);
    if (g_relay) ch_relay_want(0);
    g_next_send = g_want + 1;
    g_synced = 1;
    g_receive_only = 0;
    capture_start(cpu);
    g_over = over;
    if (over) g_synced = 0;
    return over;
}

/* The opponent's file (checked, in g_buf) is here: watch it, then load it.
 * Returns -2 while the replay runs (a game routine is called; the hook
 * must return and call ch_turns_poll() when it is back), otherwise as
 * finish_load(). */
#define FN_SWAP_POINT   0x8008A2B8u
static int deliver(CPUState *cpu, uint32_t len)
{
    if (replay_begin(cpu, len, FN_SWAP_POINT) && replay_step(cpu))
        return -2;
    return finish_load(cpu, len);
}

/* Where hot-seat would swap the sides. The waiting side has been put back.
 * Returns 1 if the game is over (no swap and no more turns), 0 if the
 * opponent's turn has been loaded, or -1 if it has not arrived yet: then
 * call ch_turns_poll() once a frame until it returns 0 or 1. -2: the replay
 * has called one of the game's routines (cpu->pc is set, returning to the
 * swap point); call ch_turns_poll() when it comes back. */
int ch_turns_swap_point(CPUState *cpu, int game_over)
{
    if (g_held) {
        const uint32_t units = cpu->read_word(PTR_UNIT_TABLE);
        for (int i = 0; i < 32; i++) cpu->write_byte(units + i * UNIT_SIZE, g_held_owner[i]);
        g_held = 0;
    }
    if (g_receive_only) {
        /* Catch up: the opponent's turn after my newest one. Player A with
         * no turn files is only here when the relay could not be reached:
         * wait until it can (g_want 0), then decide. */
        g_want = latest_seq(1) + 1;
        if (seq_is_mine(g_want)) g_want = 0;
    } else {
        send_file(cpu, game_over);
        if (game_over) {
            g_synced = 0;
            return 1;
        }
        g_want = g_next_send + 1;
    }
    const uint32_t len = g_want ? read_file(g_want) : 0;
    if (len)
        return deliver(cpu, len);
    if (g_want)
        fprintf(stderr, "[turns] waiting for turn file %u from player %c (the game keeps running)\n",
                (unsigned)g_want, g_want & 1u ? 'A' : 'B');
    else
        fprintf(stderr, "[turns] waiting for the relay before playing turn 1\n");
    set_waiting_status(1);
    if (g_relay) ch_relay_want(g_want);
    g_polls = 0;
    return -1;
}

/* Called once a frame while waiting (and after each routine the replay
 * calls). Looks for the file twice a second. Returns -2 while the replay
 * runs (as ch_turns_swap_point), -1 while still waiting, 0 when the opponent's turn was loaded (or
 * when there is nothing to wait for after all), 1 when the game is over. */
int ch_turns_poll(CPUState *cpu)
{
    if (g_rp_active)
        return replay_step(cpu) ? -2 : finish_load(cpu, g_rp_len);
    if (++g_polls % 30u != 0)
        return -1;
    set_waiting_status(1);
    if (g_want == 0) {
        /* Player A, no turn files, relay not reached at the start. */
        if (ch_relay_ready() != 1) return -1;
        g_want = latest_seq(1) + 1;
        if (seq_is_mine(g_want)) {
            /* The relay has no game yet: nothing to receive. End this turn
             * without changes; the next End Turn plays turn 1. */
            fprintf(stderr, "[turns] the relay is reachable now; press End Turn to play turn 1\n");
            show_status("Connected to the relay. Press End Turn to play turn 1.");
            if (g_relay) ch_relay_want(0);
            g_want = 0;
            return 0;
        }
        if (g_relay) ch_relay_want(g_want);
        return -1;
    }
    if (!seq_exists(g_want)) {
        if (g_polls % (30u * 120u) == 0)
            fprintf(stderr, "[turns] still waiting for turn file %u\n", (unsigned)g_want);
        return -1;
    }
    const uint32_t len = read_file(g_want);
    return len ? deliver(cpu, len) : -1;
}

/* ---- catching up when the map opens ----------------------------------- */

/* Load my own turn file seq (the state I left when I sent it), turned back
 * to my point of view. The opponent's designs come from their file before
 * it, if there is one (otherwise hot-seat's set-up gave them the stock
 * designs). */
static int load_own_file(CPUState *cpu, uint32_t seq)
{
    if (seq > 1) {
        const uint32_t plen = read_file(seq - 1);
        if (plen) memcpy(ch_hotseat_other_cards(), g_buf + TF_HEADER + TF_TABLES, TF_CARDS);
    }
    const uint32_t len = read_file(seq);
    if (!len) return 0;
    const uint32_t units = cpu->read_word(PTR_UNIT_TABLE);
    const uint32_t bases = cpu->read_word(PTR_BASE_TABLE);
    const uint32_t okes  = cpu->read_word(PTR_OKE_TABLE);
    const uint8_t *h = g_buf, *p = g_buf + TF_HEADER;
    cpu->write_word(GP_FUNDS_CPU, get32(p)); p += 4;      /* the receiver is my opponent */
    cpu->write_word(GP_FUNDS_PLAYER, get32(p)); p += 4;
    for (int i = 0; i < UNIT_COUNT; i++, p += UNIT_SIZE) {
        uint8_t u[UNIT_SIZE];
        memcpy(u, p, UNIT_SIZE);
        u[0] = swap_owner(u[0]);
        wr(cpu, units + ((i + 32) % UNIT_COUNT) * UNIT_SIZE, u, UNIT_SIZE);
    }
    for (int i = 0; i < BASE_COUNT; i++, p += BASE_SIZE) {
        wr(cpu, bases + i * BASE_SIZE, p, BASE_SIZE);
        cpu->write_byte(bases + i * BASE_SIZE, swap_owner(p[0]));
    }
    for (int i = 0; i < OKE_COUNT; i++, p += OKE_SIZE) {
        wr(cpu, okes + i * OKE_SIZE, p, OKE_SIZE);
        cpu->write_byte(okes + i * OKE_SIZE, swap_owner(p[0]));
    }
    /* The counters in my file are the receiver's; mine had already gone on
     * by one when I was A (A's turn n and B's turn n make one round). */
    const uint32_t ahead = g_role == 0 ? 1u : 0u;
    cpu->write_word(GP_TURN, get32(h + 12) + ahead);
    cpu->write_word(GP_TURN2, get32(h + 16) + ahead);
    const uint32_t cur = cpu->read_word(GP_CUR_BASE);
    if (cur >= BASE_COUNT || cpu->read_byte(bases + cur * BASE_SIZE) != 1)
        for (uint32_t b = 0; b < BASE_COUNT; b++)
            if (cpu->read_byte(bases + b * BASE_SIZE) == 1) { cpu->write_word(GP_CUR_BASE, b); break; }
    fprintf(stderr, "[turns] loaded my own turn file %u: turn %u, funds %u (opponent %u)\n",
            (unsigned)seq, cpu->read_word(GP_TURN), cpu->read_word(GP_FUNDS_PLAYER),
            cpu->read_word(GP_FUNDS_CPU));
    return 1 + (h[7] & 1);
}

/* Scenario and map of turn file seq, or -1 if it can't be read. */
static int scenario_of(uint32_t seq, int *map)
{
    char path[600];
    uint8_t h[TF_HEADER];
    path_for(path, sizeof path, seq);
    FILE *f = fopen(path, "rb");
    if (!f) return -1;
    const size_t n = fread(h, 1, TF_HEADER, f);
    fclose(f);
    if (n != TF_HEADER || memcmp(h, "CHTF", 4) != 0 || get16(h + 4) < TF_MIN_VERSION ||
        get16(h + 4) > TF_VERSION) return -1;
    *map = h[49];
    return h[48];
}

/* Online play is off for the rest of this session: the map plays as a
 * normal campaign against the CPU, and nothing is loaded or sent. */
static void switch_off(const char *why)
{
    fprintf(stderr, "[turns] %s; online play is off until the game is restarted\n", why);
    if (g_relay) ch_relay_want(0);
    g_on = 0;
    g_synced = 0;
    ch_hotseat_off();
    ch_remote_battle_off();
}

/* Is the scenario that was started the right one for this online game?
 * Returns 0 (and switches online play off) if not. */
static int check_scenario(CPUState *cpu)
{
    char msg[128], why[160];
    g_scen = (int)cpu->read_word(GP_SCENARIO);
    g_map = (int)cpu->read_word(GP_MAP);
    int map = -1, scen = -1;
    for (uint32_t s = 1; scen < 0 && seq_exists(s); s++)
        scen = scenario_of(s, &map);
    if (scen < 0) {
        /* No turn files yet (or only unreadable ones): a new game. */
        if (g_scen >= 0 && g_scen < 32 && (CH_ONLINE_SCENARIOS >> g_scen) & 1u) return 1;
        snprintf(msg, sizeof msg, "Online games are Europa (Practice) only for now. "
                 "Restart and choose it.");
        snprintf(why, sizeof why, "map start: %s started, but online games are Europa (Practice) only",
                 scenario_name(g_scen, g_map));
    } else if (scen == g_scen && map == g_map) {
        fprintf(stderr, "[turns] map start: %s, as in the turn files\n", scenario_name(g_scen, g_map));
        return 1;
    } else {
        snprintf(msg, sizeof msg, "This online game is on %s. Restart and choose %s.",
                 scenario_name(scen, map), scenario_name(scen, map));
        snprintf(why, sizeof why, "map start: %s started, but this online game is on %s",
                 scenario_name(g_scen, g_map), scenario_name(scen, map));
    }
    g_scen = g_map = -1;
    switch_off(why);
    show_status(msg);
    return 0;
}

#define FN_MAP_START    0x800333F8u

/* The opponent's newest turn has been loaded at the map start. */
static void caught_up(CPUState *cpu, int over)
{
    char msg[128];
    (void)cpu;
    fprintf(stderr, "[turns] map start: caught up with turn file %u; my turn\n", (unsigned)g_want);
    if (over) {
        show_status("This online game is over.");
    } else {
        snprintf(msg, sizeof msg, "Caught up with player %c's turn. It's your turn.", g_role ? 'A' : 'B');
        show_status(msg);
    }
}

/* Returns 1 if the replay of the opponent's turn has started (the hook runs
 * it, then calls caught_up()), else 0. */
static int catch_up_at_map_start(CPUState *cpu)
{
    if (g_relay) {
        show_status("Catching up with the relay...");
        const int r = ch_relay_wait_ready(10000);
        if (r != 1)
            fprintf(stderr, "[turns] map start: could not reach the relay; using the turn files here\n");
    }
    if (!check_scenario(cpu))
        return 0;
    ch_hotseat_setup_now(cpu);
    g_synced = 0;
    g_receive_only = 0;
    g_have_start = 0;
    g_over = 0;
    const uint32_t n = latest_seq(0);
    char msg[128];
    const char other = g_role ? 'A' : 'B';
    if (n == 0) {
        if (g_role == 0) {
            if (g_relay && ch_relay_ready() != 1)
                show_status("Can't reach the relay. Check the connection, then press End Turn.");
            else
                show_status("New online game: you play turn 1. Give your orders and press End Turn.");
        } else {
            show_status("Waiting for player A's first turn. Press End Turn to wait for it here.");
        }
        fprintf(stderr, "[turns] map start: no turn files yet\n");
        return 0;
    }
    if (!seq_is_mine(n)) {
        const uint32_t len = read_file(n);
        if (!len) { show_status(""); return 0; }
        g_want = n;
        if (replay_begin(cpu, len, FN_MAP_START))
            return 1;               /* caught_up() when it is over */
        caught_up(cpu, finish_load(cpu, len));
        return 0;
    }
    const int r = load_own_file(cpu, n);
    if (!r) { show_status(""); return 0; }
    g_prev_hash = 0;
    if (r == 2) {
        g_over = 1;
        show_status("This online game is over.");
    } else {
        snprintf(msg, sizeof msg, "It's player %c's turn. Close the game, or press End Turn to wait.", other);
        show_status(msg);
    }
    fprintf(stderr, "[turns] map start: my turn file %u is the newest; waiting for player %c\n",
            (unsigned)n, other);
    return 0;
}

/* 0x800333F8: the last step of the map start (0x8003188C), called from
 * nowhere else. The tables are set up (new map) or loaded (from a save). */
void ch_remote_lle_func_800333F8(CPUState *cpu);
static int g_ms_replay;          /* the map start is running a replay */
static uint32_t g_ms_gpr[32], g_ms_hi, g_ms_lo;

void func_800333F8(CPUState *cpu)
{
    if (g_ms_replay) {
        /* Back from one of the game's unit routines. */
        if (replay_step(cpu))
            return;
        g_ms_replay = 0;
        memcpy(cpu->gpr, g_ms_gpr, sizeof g_ms_gpr);
        cpu->hi = g_ms_hi;
        cpu->lo = g_ms_lo;
        cpu->pc = 0;
        caught_up(cpu, finish_load(cpu, g_rp_len));
    } else if (cpu->pc == 0 && ch_turns_active()) {
        memcpy(g_ms_gpr, cpu->gpr, sizeof g_ms_gpr);
        g_ms_hi = cpu->hi;
        g_ms_lo = cpu->lo;
        if (catch_up_at_map_start(cpu)) {
            if (replay_step(cpu)) {
                g_ms_replay = 1;
                return;
            }
            caught_up(cpu, finish_load(cpu, g_rp_len));   /* nothing moved */
        }
    }
    ch_remote_lle_func_800333F8(cpu);
}
