/*
 * Turn-file simulator: runs src/turn_files.c, src/hotseat.c and
 * src/remote_battle.c against a fake PS1 RAM and a stand-in End Turn that
 * calls the hooks in the same order as the game's End Turn (0x8001B8EC, see
 * docs/research/CAMPAIGN_RAM_MAP.md). It checks the protocol (who moves
 * when, what is sent and loaded, turn numbers, catching up after a restart),
 * not the game itself. Run two copies, one per player, on one folder:
 *   run.sh   (builds, plays A and B against each other, checks the logs)
 *
 * sim <A|B> <turns> : opens the map (the map-start hook catches up), then
 * plays <turns> End Turns, printing PRE/POST lines.
 * SIM_QUIT_WAITING=1: close the game while waiting after the last End Turn
 * (the turn is sent; the opponent's is not there yet).
 * SIM_DELAY_MS=n: think for n ms before each End Turn.
 * SIM_SCENARIO=n, SIM_MAP=m: the scenario and map that were started (default
 * 0 and 0, Europa (Practice)).
 */
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include "cpu_state.h"
static long g_frames_waited;
static int g_last_turn;            /* the End Turn being played is the last one */
static char g_status[160];

#define GP_TURN         0x800DF37Cu
#define GP_TURN2        0x800DF380u
#define GP_FUNDS_PLAYER 0x800DF38Cu
#define GP_FUNDS_CPU    0x800DF390u
#define PTR_PLAYER_CARD 0x800E0014u
#define PTR_OKE_TABLE   0x800E014Cu
#define PTR_BASE_TABLE  0x800E0154u
#define PTR_UNIT_TABLE  0x800E017Cu
#define CARDS 0x80100000u
#define OKES  0x80110000u
#define BASES 0x80112000u
#define UNITS 0x80134B6Cu

static uint8_t ram[0x200000];
static uint32_t rw(uint32_t a) { a &= 0x1FFFFF; return ram[a] | ram[a+1] << 8 | ram[a+2] << 16 | (uint32_t)ram[a+3] << 24; }
static void ww(uint32_t a, uint32_t v) { a &= 0x1FFFFF; ram[a] = v; ram[a+1] = v >> 8; ram[a+2] = v >> 16; ram[a+3] = v >> 24; }
static uint16_t rh(uint32_t a) { a &= 0x1FFFFF; return ram[a] | ram[a+1] << 8; }
static void wh(uint32_t a, uint16_t v) { a &= 0x1FFFFF; ram[a] = v; ram[a+1] = v >> 8; }
static uint8_t rb(uint32_t a) { return ram[a & 0x1FFFFF]; }
static void wb(uint32_t a, uint8_t v) { ram[a & 0x1FFFFF] = v; }
static CPUState C = { .read_word = rw, .write_word = ww, .read_half = rh,
                      .write_half = wh, .read_byte = rb, .write_byte = wb };

/* The hooks under test. */
void func_8001B8EC(CPUState *); void func_8001C268(CPUState *); void func_8001BCB0(CPUState *);
void func_8008A2B8(CPUState *); void func_8001CF48(CPUState *); void func_80035CD4(CPUState *);
void func_80035988(CPUState *); void func_800333F8(CPUState *); void func_8008E1E4(CPUState *);
void func_8004BBB8(CPUState *); void func_800566F4(CPUState *); void func_80049828(CPUState *);
void func_80049F44(CPUState *); void func_8004A214(CPUState *); void func_80049CA4(CPUState *);

/* Negotiation (negotiation_sync.c): a few blueprints. Part 0 is a stock
 * blueprint both own, part 1 is on sale, part 2 is in R&D in slot 0
 * (3 turns left), part 3 starts in R&D in slot 1 (6 turns left). Part 2's
 * price is 5000 (parts table +0x1C). */
#define NEG_PARTS 0x80136100u
#define NEG_SLOTS 0x801365A0u
#define NEG_COMPS 0x801360CCu
#define P(i) (NEG_PARTS + (i) * 4u)
#define SL(i) (NEG_SLOTS + (i) * 12u)
static int g_cpu_neg_ran;

/* The game routines the hooks may "call" (cpu->pc set, return address in
 * $ra): VSync while waiting; the unit steps while replaying the opponent's
 * turn (turn_files.c). */
#define FN_VSYNC     0x800AC734u
#define FN_UNIT_PATH 0x80034C38u
#define FN_UNIT_SHOW 0x8001BCB0u
#define FN_UNIT_MOVE 0x8001C000u
#define FN_TURN_PREP 0x800341FCu
static void move_one(int i, int player_side);
static long g_replay_moves;

#define U(i) (UNITS + (i) * 22u)
#define O(k) (OKES + (k) * 12u)
#define B(b) (BASES + (b) * 92u)

static int g_cpu_ai_ran, g_wrong_mover;

static void battle(uint32_t u1, uint32_t u2)
{
    C.pc = 0; C.gpr[4] = u1; C.gpr[5] = u2;
    func_80035CD4(&C);
    func_8001CF48(&C);
    C.pc = 0; C.gpr[4] = u1; func_80035988(&C);
    C.pc = 0; C.gpr[4] = u2; func_80035988(&C);
}

/* One unit's step (0x8001C000): one hex towards its target; a battle if an
 * enemy unit is there (the player-side unit is always side 1), then base
 * capture. */
static void move_one(int i, int player_side)
{
    if (rb(U(i)) == 0 || rb(U(i) + 1) == 0) return;
    if (rb(U(i)) != (player_side ? 1 : 2)) g_wrong_mover++;
    int16_t x = (int16_t)rh(U(i) + 2), t = (int16_t)rh(U(i) + 6);
    if (x == t) return;
    x += x < t ? 1 : -1;
    wh(U(i) + 2, (uint16_t)x);
    for (int j = 0; j < 64; j++)
        if (j != i && rb(U(j)) && rb(U(j)) != rb(U(i)) && (int16_t)rh(U(j) + 2) == x) {
            if (player_side) battle(i, j); else battle(j, i);
            break;
        }
    /* base capture */
    for (int b = 0; b < 2; b++)
        if (rb(U(i)) && (int16_t)rh(B(b) + 2) == x && rb(B(b)) != rb(U(i))) {
            wb(B(b), rb(U(i)));
            for (int k = 0; k < 200; k++) if (rb(O(k)) && rb(O(k) + 1) == 0 && rb(O(k) + 2) == b) wb(O(k), 0);
        }
}

static void move_units(int lo, int hi)
{
    for (int i = lo; i < hi; i++)
        move_one(i, lo == 0);
}

/* Run a hook, then every game routine it "calls", returning to it each
 * time, until it hands control back (pc is no longer one of them). */
static void run_hook(void (*hook)(CPUState *), uint32_t addr, int waiting_quit)
{
    CPUState *c = &C;
    hook(c);
    for (int frames = 0;; frames++) {
        const uint32_t pc = c->pc;
        if (pc != FN_VSYNC && pc != FN_UNIT_PATH && pc != FN_UNIT_SHOW && pc != FN_UNIT_MOVE &&
            pc != FN_TURN_PREP)
            break;
        if (c->gpr[31] != addr) { fprintf(stderr, "SIM: hook call lost its way back\n"); exit(2); }
        c->gpr[16] = 0xDEAD;            /* a routine may use any register */
        if (pc == FN_VSYNC) {
            struct timespec ts = { 0, 1000000L }; nanosleep(&ts, NULL);
            g_frames_waited = frames + 1;
            if (waiting_quit && g_last_turn && frames > 3 && getenv("SIM_QUIT_WAITING")) {
                printf("QUIT while waiting: %s\n", g_status);
                fflush(stdout);
                exit(0);
            }
        } else if (pc == FN_UNIT_MOVE) {
            if (c->gpr[4] < 32) { fprintf(stderr, "SIM: replay moved my unit %u\n", c->gpr[4]); exit(2); }
            move_one((int)c->gpr[4], 0);
            g_replay_moves++;
        }
        c->pc = addr;                   /* the routine returns to the hook's entry */
        hook(c);
    }
}

static int g_role;
static void side(char *out, int mine);
static void print_side(const char *tag, int mine)
{
    static char s[8192];
    side(s, mine);
    printf("%s %c %s turn=%u %s\n", tag, "AB"[g_role], mine ? "mine  " : "theirs", rw(GP_TURN), s);
}

/* ---- stand-ins for the original functions ---- */
void ch_remote_lle_func_8001B8EC(CPUState *c)
{
    ww(GP_TURN, rw(GP_TURN) + 1); ww(GP_TURN2, rw(GP_TURN2) + 1);
    move_units(0, 32);
    c->pc = 0; c->gpr[31] = 0x1234; func_8001C268(c);
    if (c->pc != 0x1234) { fprintf(stderr, "SIM: brain hook did not return\n"); exit(2); }
    move_units(32, 64);
    c->pc = 0; func_8001BCB0(c);
    /* research and negotiation (0x8004BBB8), then player production
     * (0x800566F4), which the negotiation hook uses as its "after" step */
    c->pc = 0; c->gpr[31] = 0x1236; func_8004BBB8(c);
    if (c->pc != 0 && c->pc != 0x1236) { fprintf(stderr, "SIM: research hook lost its way\n"); exit(2); }
    c->pc = 0; func_800566F4(c);
    /* production / research: both funds change, both sides' bases get fuel */
    ww(GP_FUNDS_PLAYER, rw(GP_FUNDS_PLAYER) - 100);
    ww(GP_FUNDS_CPU, rw(GP_FUNDS_CPU) - 7);
    for (int b = 0; b < 2; b++) ww(B(b) + 12, rw(B(b) + 12) + 10);
    print_side("PRE", 1);
    /* The swap-point hook keeps the game running while it waits: it "calls"
     * VSync (0x800AC734) with itself as the return address. Play that out:
     * one frame, then back into the hook, until it returns to End Turn with
     * the registers End Turn called it with. */
    c->pc = 0; c->gpr[16] = 0x5150; c->gpr[31] = 0x8001BAAA;
    run_hook(func_8008A2B8, 0x8008A2B8u, 1);
    if (c->gpr[16] != 0x5150 || c->gpr[31] != 0x8001BAAA) {
        fprintf(stderr, "SIM: registers not put back after waiting\n"); exit(2);
    }
}
void host_osd_set_status(const char *msg)
{
    if (strcmp(msg, g_status) == 0) return;
    snprintf(g_status, sizeof g_status, "%s", msg);
    if (*msg) printf("STATUS %s\n", msg);
}
void ch_remote_lle_func_800333F8(CPUState *c) { (void)c; }
/* mouse_pad_hle.c's skip check for the replay: with SIM_SKIP=n, "press"
 * skip on the n-th check of each replay (1 = before any of their units
 * moves). */
int ch_replay_skip_poll(CPUState *c, int arm)
{
    static int calls;
    (void)c;
    if (arm) { calls = 0; return 0; }
    const char *e = getenv("SIM_SKIP");
    return e && ++calls == atoi(e);
}
/* The CPU's negotiation: must not run in a versus game. */
static void cpu_neg(CPUState *c) { g_cpu_neg_ran++; ww(GP_FUNDS_CPU, rw(GP_FUNDS_CPU) - 1000); wh(P(1), rh(P(1)) | 4); c->gpr[2] = 1000; c->pc = c->gpr[31]; }
void ch_remote_lle_func_80049828(CPUState *c) { cpu_neg(c); }
void ch_remote_lle_func_80049F44(CPUState *c) { cpu_neg(c); }
void ch_remote_lle_func_8004A214(CPUState *c) { cpu_neg(c); }
void ch_remote_lle_func_80049CA4(CPUState *c) { cpu_neg(c); }
/* The companies' research: each blueprint in R&D gets a turn closer; at 0
 * it goes on sale (the slot is freed). Then the game calls the CPU's
 * negotiation. */
void ch_remote_lle_func_8004BBB8(CPUState *c)
{
    for (int s = 0; s < 32; s++) {
        int16_t part = (int16_t)rh(SL(s));
        if (part < 0) continue;
        uint16_t f = rh(P(part));
        uint8_t t = rb(P(part) + 2);
        if (!(f & 8)) continue;
        if ((t & 63) > 1) { wb(P(part) + 2, (uint8_t)(t - 1)); continue; }
        wh(P(part), (uint16_t)((f | 1) & ~8)); wb(P(part) + 2, (uint8_t)(t & 0xC0)); wb(P(part) + 3, 0);
        wh(SL(s), 0xFFFF);
    }
    const uint32_t ra = c->gpr[31];
    c->pc = 0; c->gpr[31] = 0x4343; func_80049828(c);
    c->pc = 0; c->gpr[31] = 0x4343; func_80049F44(c);
    c->pc = 0; c->gpr[31] = 0x4343; func_8004A214(c);
    c->pc = 0; c->gpr[31] = 0x4343; func_80049CA4(c);
    c->gpr[31] = ra; c->pc = 0;
}
void ch_remote_lle_func_800566F4(CPUState *c) { (void)c; }
void ch_remote_lle_func_8001C268(CPUState *c) { g_cpu_ai_ran++; c->pc = c->gpr[31]; }
void ch_remote_lle_func_8001BCB0(CPUState *c) { (void)c; }
void ch_remote_lle_func_8008A2B8(CPUState *c) { (void)c; }
void ch_remote_lle_func_8001CF48(CPUState *c) { (void)c; }
static int sim_rand(void)
{
    CPUState save = C;
    C.pc = 0; C.gpr[31] = 0x4242;
    func_8008E1E4(&C);
    const int r = (int)C.gpr[2];
    if (C.pc != 0x4242) { fprintf(stderr, "SIM: rand did not return\n"); exit(2); }
    uint32_t keep[32]; memcpy(keep, save.gpr, sizeof keep);
    C = save; memcpy(C.gpr, keep, sizeof keep);
    return r;
}
void ch_remote_lle_func_80035CD4(CPUState *c)
{   /* the battle: side 1 loses 30-69 HP per OKE, side 2 160-239 (from rand,
     * so a replay only matches if it starts from the same seed) */
    const uint32_t u1 = c->gpr[4], u2 = c->gpr[5];
    ww(0x800DF39Cu, rw(0x800DF39Cu) + 1);   /* battles, as the game counts them */
    for (int side = 0; side < 2; side++) {
        uint32_t u = U(side ? u2 : u1);
        for (int s = 0; s < 3; s++) {
            int16_t k = (int16_t)rh(u + 12 + 2 * s);
            int dmg = side ? 160 + sim_rand() % 80 : 30 + sim_rand() % 40;
            if (side && getenv("SIM_LETHAL")) dmg += 400;   /* side 2 is wiped out */
            if (k >= 0) wh(O(k) + 6, (uint16_t)((int16_t)rh(O(k) + 6) - dmg));
        }
    }
}
void ch_remote_lle_func_8008E1E4(CPUState *c) { c->gpr[2] = 7; c->pc = c->gpr[31]; }
void ch_remote_lle_func_80035988(CPUState *c)
{
    uint32_t u = U(c->gpr[4]); int left = 0;
    for (int s = 0; s < 3; s++) {
        int16_t k = (int16_t)rh(u + 12 + 2 * s);
        if (k < 0) continue;
        if ((int16_t)rh(O(k) + 6) <= 0) { wb(O(k), 0); wh(u + 12 + 2 * s, 0xFFFF); } else left++;
    }
    if (!left) {
        if (rb(u) == 2) ww(0x800DF398u, rw(0x800DF398u) + 1);   /* an enemy unit wiped out */
        wb(u, 0);
    }
}

static void init(int role)
{
    memset(ram, 0, sizeof ram);
    ww(PTR_PLAYER_CARD, CARDS); ww(PTR_OKE_TABLE, OKES); ww(PTR_BASE_TABLE, BASES); ww(PTR_UNIT_TABLE, UNITS);
    ww(0x800DFFFCu, NEG_PARTS); ww(0x800DFF6Cu, NEG_SLOTS); ww(0x800E00B4u, NEG_COMPS);
    for (int s = 0; s < 32; s++) wh(SL(s), 0xFFFF);
    wh(P(0), 7); wh(P(1), 1);
    wh(P(2), 8); wb(P(2) + 2, 3); wb(P(2) + 3, 100); wh(SL(0), 2);
    wh(P(3), 8); wb(P(3) + 2, 6); wb(P(3) + 3, 90); wh(SL(1), 3);
    ww(0x800C0C7Cu + 2 * 36u + 0x1Cu, 5000);
    wh(0x800BD0ACu + 10, 2); wh(0x800BD0ACu + 12, 0xFFFF);   /* company 0 sells part 2 */
    ww(GP_TURN, 1); ww(GP_TURN2, 1); ww(GP_FUNDS_PLAYER, 500000); ww(GP_FUNDS_CPU, 300000);
    if (getenv("SIM_SCENARIO")) ww(0x800DF384u, (uint32_t)atoi(getenv("SIM_SCENARIO")));
    if (getenv("SIM_MAP")) ww(0x800DF388u, (uint32_t)atoi(getenv("SIM_MAP")));
    for (int c = 0; c < 28; c++) {   /* designs: marked by player and slot */
        memset(ram + ((CARDS + c * 1152u) & 0x1FFFFF), (role ? 0xB0 : 0xA0) + (c & 15), 1152);
        wh(CARDS + c * 1152u + 52, 520);
    }
    wb(B(0), 1); wh(B(0) + 2, 8);  wb(B(1), 2); wh(B(1) + 2, 20);
    for (int k = 0; k < 12; k++) { wb(O(k), k < 6 ? 1 : 2); wb(O(k) + 2, k < 6 ? 0 : 1); wb(O(k) + 3, 25); wh(O(k) + 6, 520); }
    for (int i = 0; i < 64; i++) for (int s = 0; s < 3; s++) wh(U(i) + 12 + 2 * s, 0xFFFF);
}

/* My turn's orders: on turn 1 form a unit from my first 3 stored OKEs and
 * send it to capture the enemy base; on turn 3 re-order it (still capture). */
static void play(void)
{
    uint32_t t = rw(GP_TURN);
    int mybase = rb(B(0)) == 1 ? 0 : 1;
    if (t == 1) {
        int n = 0;
        wb(U(0), 1); wb(U(0) + 1, 3); wh(U(0) + 2, rh(B(mybase) + 2)); wh(U(0) + 6, rh(B(1 - mybase) + 2));
        for (int k = 0; k < 200 && n < 3; k++)
            if (rb(O(k)) == 1 && rb(O(k) + 1) == 0) { wb(O(k) + 1, 1); wh(U(0) + 12 + 2 * n++, (uint16_t)k); }
    } else if (t == 3 && rb(U(0)) == 1) {
        wh(U(0) + 8, 99);   /* a re-order: home cell changes */
    }
    /* Negotiation: A buys blueprint 1 on turn 1; B invests 9000 in
     * blueprint 2 (slot 0) on turn 1, enough for 20% off when it is done. */
    if (t == 1 && g_role == 0) {
        wh(P(1), rh(P(1)) | 2); ww(GP_FUNDS_PLAYER, rw(GP_FUNDS_PLAYER) - 4000);
        wh(NEG_COMPS + 0, rh(NEG_COMPS + 0) + 4); wh(NEG_COMPS + 2, rh(NEG_COMPS + 2) + 1);
    }
    if (t == 1 && g_role == 1 && (int16_t)rh(SL(0)) == 2) {
        ww(SL(0) + 4, rw(SL(0) + 4) + 9000); wh(P(2), rh(P(2)) | 0x2000);
        ww(GP_FUNDS_PLAYER, rw(GP_FUNDS_PLAYER) - 9000);
    }
}

/* One side of the board, numbered from that side's own point of view. */
static void side(char *out, int mine)
{
    int pos = 0, own = mine ? 1 : 2;
    pos += sprintf(out + pos, "funds=%u units=", rw(mine ? GP_FUNDS_PLAYER : GP_FUNDS_CPU));
    for (int i = 0; i < 32; i++) {
        int s = mine ? i : i + 32;
        if (rb(U(s)) == own)
            pos += sprintf(out + pos, "[%d x%d t%d h%d]", i, (int16_t)rh(U(s) + 2), rh(U(s) + 6), rh(U(s) + 8));
    }
    pos += sprintf(out + pos, " okes=");
    for (int k = 0; k < 200; k++)
        if (rb(O(k)) == own) pos += sprintf(out + pos, "%d:%d,", k, (int16_t)rh(O(k) + 6));
    pos += sprintf(out + pos, " bases=");
    for (int b = 0; b < 16; b++) if (rb(B(b)) == own) pos += sprintf(out + pos, "%d,", b);
    pos += sprintf(out + pos, " owns=");
    for (int i = 0; i < 4; i++) if (rh(P(i)) & (mine ? 2 : 4)) pos += sprintf(out + pos, "%d,", i);
    pos += sprintf(out + pos, " invested=");
    for (int s = 0; s < 2; s++) pos += sprintf(out + pos, "%u,", rw(SL(s) + (mine ? 4 : 8)));
    pos += sprintf(out + pos, " rd=");
    for (int i = 0; i < 4; i++) pos += sprintf(out + pos, "%d:%s%d,", i, (rh(P(i)) & 1) ? "sale" : "t", rb(P(i) + 2) & 63);
}

int main(int argc, char **argv)
{
    if (argc < 3) { fprintf(stderr, "usage: sim A|B turns\n"); return 1; }
    const int role = g_role = argv[1][0] == 'B';
    const int turns = atoi(argv[2]);
    init(role);
    setvbuf(stdout, NULL, _IONBF, 0);
    /* The map opens: catch up with the newest turn file. */
    C.pc = 0; C.gpr[16] = 0x5150; C.gpr[31] = 0x80031AAA;
    run_hook(func_800333F8, 0x800333F8u, 0);
    if (C.gpr[16] != 0x5150 || C.gpr[31] != 0x80031AAA) {
        fprintf(stderr, "SIM: registers not put back after the map start\n"); exit(2);
    }
    if (strncmp(g_status, "Caught up", 9) == 0) {
        /* Same lines as a receive-only End Turn would print. */
        print_side("PRE", 1);
        print_side("POST", 1);
        print_side("POST", 0);
    } else if (strncmp(g_status, "It's player", 11) == 0) {
        print_side("OWN", 1);
    }
    const char *delay = getenv("SIM_DELAY_MS");
    for (int n = 0; n < turns; n++) {
        if (delay) { struct timespec ts = { atoi(delay) / 1000, (atoi(delay) % 1000) * 1000000L }; nanosleep(&ts, NULL); }
        g_last_turn = n == turns - 1;
        play();
        C.pc = 0;
        func_8001B8EC(&C);
        print_side("POST", 1);
        print_side("POST", 0);
        printf("NEG %c part2 flags=%04x discount=%d part1 mine=%d\n", "AB"[role], rh(P(2)),
               rb(P(2) + 2) >> 6, (rh(P(1)) & 2) != 0);
        if (rw(GP_FUNDS_PLAYER) == 0) break;
    }
    printf("END %c cpu_ai_ran=%d wrong_mover=%d frames_waited_last=%ld replay_moves=%ld\n", "AB"[role],
           g_cpu_ai_ran, g_wrong_mover, g_frames_waited, g_replay_moves);
    if (g_cpu_neg_ran) printf("CPU negotiation ran %d time(s)\n", g_cpu_neg_ran);
    return (g_cpu_ai_ran || g_wrong_mover || g_cpu_neg_ran) ? 3 : 0;
}
