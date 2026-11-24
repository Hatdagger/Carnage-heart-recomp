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
void func_80035988(CPUState *); void func_800333F8(CPUState *);

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

static void move_units(int lo, int hi)
{
    for (int i = lo; i < hi; i++) {
        if (rb(U(i)) == 0 || rb(U(i) + 1) == 0) continue;
        if (rb(U(i)) != (lo == 0 ? 1 : 2)) g_wrong_mover++;
        int16_t x = (int16_t)rh(U(i) + 2), t = (int16_t)rh(U(i) + 6);
        if (x == t) continue;
        x += x < t ? 1 : -1;
        wh(U(i) + 2, (uint16_t)x);
        for (int j = 0; j < 64; j++)
            if (j != i && rb(U(j)) && rb(U(j)) != rb(U(i)) && (int16_t)rh(U(j) + 2) == x) {
                if (lo == 0) battle(i, j); else battle(j, i);
                break;
            }
        /* base capture */
        for (int b = 0; b < 2; b++)
            if (rb(U(i)) && (int16_t)rh(B(b) + 2) == x && rb(B(b)) != rb(U(i))) {
                wb(B(b), rb(U(i)));
                for (int k = 0; k < 200; k++) if (rb(O(k)) && rb(O(k) + 1) == 0 && rb(O(k) + 2) == b) wb(O(k), 0);
            }
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
    func_8008A2B8(c);
    for (int frames = 0; c->pc == 0x800AC734u; frames++) {
        if (c->gpr[31] != 0x8008A2B8u) { fprintf(stderr, "SIM: wait loop lost its way back\n"); exit(2); }
        c->gpr[16] = 0xDEAD;            /* VSync may use any register */
        struct timespec ts = { 0, 1000000L }; nanosleep(&ts, NULL);
        c->pc = 0x8008A2B8u;            /* VSync returns to the hook's entry */
        func_8008A2B8(c);
        g_frames_waited = frames + 1;
        if (g_last_turn && frames > 3 && getenv("SIM_QUIT_WAITING")) {
            printf("QUIT while waiting: %s\n", g_status);
            fflush(stdout);
            exit(0);
        }
    }
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
void ch_remote_lle_func_8001C268(CPUState *c) { (void)c; g_cpu_ai_ran++; }
void ch_remote_lle_func_8001BCB0(CPUState *c) { (void)c; }
void ch_remote_lle_func_8008A2B8(CPUState *c) { (void)c; }
void ch_remote_lle_func_8001CF48(CPUState *c) { (void)c; }
void ch_remote_lle_func_80035CD4(CPUState *c)
{   /* the battle: side 1 (mover) loses 50 HP per OKE, side 2 loses 200 */
    for (int side = 0; side < 2; side++) {
        uint32_t u = U(c->gpr[4 + side]);
        for (int s = 0; s < 3; s++) {
            int16_t k = (int16_t)rh(u + 12 + 2 * s);
            if (k >= 0) wh(O(k) + 6, (uint16_t)((int16_t)rh(O(k) + 6) - (side ? 200 : 50)));
        }
    }
}
void ch_remote_lle_func_80035988(CPUState *c)
{
    uint32_t u = U(c->gpr[4]); int left = 0;
    for (int s = 0; s < 3; s++) {
        int16_t k = (int16_t)rh(u + 12 + 2 * s);
        if (k < 0) continue;
        if ((int16_t)rh(O(k) + 6) <= 0) { wb(O(k), 0); wh(u + 12 + 2 * s, 0xFFFF); } else left++;
    }
    if (!left) wb(u, 0);
}

static void init(int role)
{
    memset(ram, 0, sizeof ram);
    ww(PTR_PLAYER_CARD, CARDS); ww(PTR_OKE_TABLE, OKES); ww(PTR_BASE_TABLE, BASES); ww(PTR_UNIT_TABLE, UNITS);
    ww(GP_TURN, 1); ww(GP_TURN2, 1); ww(GP_FUNDS_PLAYER, 500000); ww(GP_FUNDS_CPU, 300000);
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
}

int main(int argc, char **argv)
{
    if (argc < 3) { fprintf(stderr, "usage: sim A|B turns\n"); return 1; }
    const int role = g_role = argv[1][0] == 'B';
    const int turns = atoi(argv[2]);
    init(role);
    setvbuf(stdout, NULL, _IONBF, 0);
    /* The map opens: catch up with the newest turn file. */
    C.pc = 0;
    func_800333F8(&C);
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
        if (rw(GP_FUNDS_PLAYER) == 0) break;
    }
    printf("END %c cpu_ai_ran=%d wrong_mover=%d frames_waited_last=%ld\n", "AB"[role], g_cpu_ai_ran, g_wrong_mover, g_frames_waited);
    return (g_cpu_ai_ran || g_wrong_mover) ? 3 : 0;
}
