/*
 * Negotiation and research in the campaign versus (hot-seat and turn files,
 * built with -DCH_REMOTE_BATTLE=ON). The RAM layout is in
 * docs/research/CAMPAIGN_RAM_MAP.md ("Negotiation and research").
 *
 * In the original game the six companies develop blueprints on their own
 * (End Turn, 0x8004BBB8), the player buys blueprints, invests in R&D and
 * orders optimizations, and the CPU (Drakken) does the same with its own
 * funds. In a versus game both players are "the player" on their own copy,
 * and the opponent stands in for Drakken.
 *
 * What this module does while hot-seat or turn files are on:
 *
 * 1. The CPU's negotiation is off. 0x80049828 (Drakken buys blueprints),
 *    0x80049F44 (Drakken orders optimizations), 0x8004A214 (Drakken invests
 *    in R&D) and 0x80049CA4 (Drakken buys a blueprint as it is finished)
 *    are skipped: they would spend the waiting side's funds.
 *
 * 2. Each player has a ledger: their own part of the negotiation state.
 *    Per blueprint: bought (flag 0x0002), asked about (0x0100), optimization
 *    ordered (0x0400) / done (0x1000), invested in R&D (0x2000), the
 *    investor's bonus (0x0800), development delayed (0x8000), ammo hidden
 *    (0x0010), the investor's discount (byte +2 bits 6-7) and, once the
 *    blueprint is finished, the optimization countdown (byte +2 bits 0-5)
 *    and byte +3. Per R&D slot: the amount invested (+4). Per company: the
 *    8-byte record (goodwill, purchases, flags). The "asked this turn"
 *    counters at 0x80128040.
 *    Everything else is the world: which blueprints exist, are being
 *    developed (turns left, chance) or are on sale, and which R&D slot
 *    holds which blueprint.
 *    RAM always holds the world, the ledger of the side whose turn it is,
 *    and the opponent's ownership shown as Drakken's (flag 0x0004, and their
 *    investment as Drakken's at slot +8), so "Drakken purchased our..." in
 *    Information means the opponent did.
 *
 * 3. The world moves on once a round, on player A's half (side A in
 *    hot-seat): the game's End Turn routine runs as usual. On B's half it
 *    runs too (B's optimizations count down, goodwill fades), but the world
 *    is put back afterwards. When a blueprint that B invested in is
 *    finished on A's half, B's bonus (discount, flag 0x0800, company flag)
 *    is worked out here the way the game does it for the player.
 *
 * Hot-seat swaps the ledgers with the sides (ch_neg_swap). Turn files carry
 * the world and both ledgers (ch_neg_write / ch_neg_read): the sender's
 * world is authoritative, like the other tables.
 */
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include "cpu_state.h"

#define PTR_PARTS_STATE 0x800DFFFCu   /* per blueprint, 4 bytes */
#define PTR_RD_SLOTS    0x800DFF6Cu   /* R&D slots, 12 bytes */
#define PTR_COMPANIES   0x800E00B4u   /* companies, 8 bytes */
#define ASKED           0x80128040u   /* 6 x s16: Information asked this turn */
#define PARTS_TABLE     0x800C0C7Cu   /* static, 36 bytes per blueprint; +0x1C price */
#define COMPANY_LISTS   0x800BD0ACu   /* static, 6 x 138 bytes; +10 list of s16, -1 ends */
#define NPARTS          297
#define NSLOTS          32
#define NCOMP           6

#define F_ON_SALE   0x0001u
#define F_MINE      0x0002u
#define F_THEIRS    0x0004u   /* Drakken's: the opponent's */
#define F_IN_RD     0x0008u
#define F_PRIVATE   (0x0002u | 0x0010u | 0x0100u | 0x0400u | 0x0800u | 0x1000u | 0x2000u | 0x8000u)

/* Sizes (bytes) of the parts of the turn-file section. */
#define WORLD_SIZE  (NPARTS * 4 + NSLOTS * 2)
#define LEDGER_SIZE (NPARTS * 4 + NSLOTS * 4 + NCOMP * 8 + NCOMP * 2)
#define NEG_SIZE    (4 + WORLD_SIZE + 2 * LEDGER_SIZE)

typedef struct {
    uint8_t parts[NPARTS][4];      /* flags (u16, private bits), +2, +3 */
    uint32_t inv[NSLOTS];
    uint8_t comp[NCOMP][8];
    uint8_t asked[NCOMP][2];
} Ledger;

typedef struct {
    uint8_t parts[NPARTS][4];      /* flags (u16, world bits), +2, +3 */
    int16_t slot_part[NSLOTS];
} World;

int ch_hotseat_active(void);
int ch_hotseat_ready(void);
int ch_hotseat_world_half(void);   /* 1: the world moves on in this half */

static Ledger g_other;             /* the side that is not "the player" now */
static int g_have_other;

/* ---- reading and writing RAM ---------------------------------------- */

static uint16_t le16(const uint8_t *p) { return (uint16_t)(p[0] | (p[1] << 8)); }
static void set16(uint8_t *p, uint16_t v) { p[0] = (uint8_t)v; p[1] = (uint8_t)(v >> 8); }

static int tables(CPUState *cpu, uint32_t *parts, uint32_t *slots, uint32_t *comps)
{
    *parts = cpu->read_word(PTR_PARTS_STATE);
    *slots = cpu->read_word(PTR_RD_SLOTS);
    *comps = cpu->read_word(PTR_COMPANIES);
    return *parts && *slots && *comps;
}

typedef struct {
    uint8_t parts[NPARTS][4];
    uint8_t slots[NSLOTS][12];
    uint8_t comp[NCOMP][8];
    uint8_t asked[NCOMP][2];
} Raw;

static int read_raw(CPUState *cpu, Raw *r)
{
    uint32_t p, s, c;
    if (!tables(cpu, &p, &s, &c)) return 0;
    for (int i = 0; i < NPARTS; i++) for (int k = 0; k < 4; k++) r->parts[i][k] = cpu->read_byte(p + i * 4 + k);
    for (int i = 0; i < NSLOTS; i++) for (int k = 0; k < 12; k++) r->slots[i][k] = cpu->read_byte(s + i * 12 + k);
    for (int i = 0; i < NCOMP; i++) for (int k = 0; k < 8; k++) r->comp[i][k] = cpu->read_byte(c + i * 8 + k);
    for (int i = 0; i < NCOMP; i++) for (int k = 0; k < 2; k++) r->asked[i][k] = cpu->read_byte(ASKED + i * 2 + k);
    return 1;
}

static void write_raw(CPUState *cpu, const Raw *r)
{
    uint32_t p, s, c;
    if (!tables(cpu, &p, &s, &c)) return;
    for (int i = 0; i < NPARTS; i++) for (int k = 0; k < 4; k++) cpu->write_byte(p + i * 4 + k, r->parts[i][k]);
    for (int i = 0; i < NSLOTS; i++) for (int k = 0; k < 12; k++) cpu->write_byte(s + i * 12 + k, r->slots[i][k]);
    for (int i = 0; i < NCOMP; i++) for (int k = 0; k < 8; k++) cpu->write_byte(c + i * 8 + k, r->comp[i][k]);
    for (int i = 0; i < NCOMP; i++) for (int k = 0; k < 2; k++) cpu->write_byte(ASKED + i * 2 + k, r->asked[i][k]);
}

/* ---- world and ledgers ------------------------------------------------ */

static void split(const Raw *r, World *w, Ledger *m)
{
    for (int i = 0; i < NPARTS; i++) {
        const uint8_t *e = r->parts[i];
        const uint16_t f = le16(e);
        const int done = f & F_ON_SALE;
        set16(w->parts[i], (uint16_t)(f & ~(F_PRIVATE | F_THEIRS)));
        w->parts[i][2] = done ? 0 : (uint8_t)(e[2] & 0x3F);
        w->parts[i][3] = done ? 0 : e[3];
        set16(m->parts[i], (uint16_t)(f & F_PRIVATE));
        m->parts[i][2] = done ? e[2] : (uint8_t)(e[2] & 0xC0);
        m->parts[i][3] = done ? e[3] : 0;
    }
    for (int i = 0; i < NSLOTS; i++) {
        w->slot_part[i] = (int16_t)le16(r->slots[i]);
        m->inv[i] = (uint32_t)(r->slots[i][4] | (r->slots[i][5] << 8) | (r->slots[i][6] << 16) |
                               ((uint32_t)r->slots[i][7] << 24));
    }
    memcpy(m->comp, r->comp, sizeof m->comp);
    memcpy(m->asked, r->asked, sizeof m->asked);
}

static void merge(Raw *r, const World *w, const Ledger *m, const Ledger *t)
{
    for (int i = 0; i < NPARTS; i++) {
        const uint16_t wf = le16(w->parts[i]);
        uint16_t f = (uint16_t)(wf | le16(m->parts[i]));
        if (le16(t->parts[i]) & F_MINE) f |= F_THEIRS;
        set16(r->parts[i], f);
        if (wf & F_ON_SALE) {
            r->parts[i][2] = m->parts[i][2];
            r->parts[i][3] = m->parts[i][3];
        } else {
            r->parts[i][2] = (uint8_t)((m->parts[i][2] & 0xC0) | (w->parts[i][2] & 0x3F));
            r->parts[i][3] = w->parts[i][3];
        }
    }
    for (int i = 0; i < NSLOTS; i++) {
        uint8_t *s = r->slots[i];
        set16(s, (uint16_t)w->slot_part[i]);
        s[2] = s[3] = 0;
        for (int k = 0; k < 4; k++) s[4 + k] = (uint8_t)(m->inv[i] >> (8 * k));
        for (int k = 0; k < 4; k++) s[8 + k] = (uint8_t)(t->inv[i] >> (8 * k));
    }
    memcpy(r->comp, m->comp, sizeof r->comp);
    memcpy(r->asked, m->asked, sizeof r->asked);
}

static void put_ledger(uint8_t *p, const Ledger *l)
{
    memcpy(p, l->parts, sizeof l->parts); p += sizeof l->parts;
    for (int i = 0; i < NSLOTS; i++, p += 4)
        for (int k = 0; k < 4; k++) p[k] = (uint8_t)(l->inv[i] >> (8 * k));
    memcpy(p, l->comp, sizeof l->comp); p += sizeof l->comp;
    memcpy(p, l->asked, sizeof l->asked);
}

static void get_ledger(const uint8_t *p, Ledger *l)
{
    memcpy(l->parts, p, sizeof l->parts); p += sizeof l->parts;
    for (int i = 0; i < NSLOTS; i++, p += 4)
        l->inv[i] = (uint32_t)(p[0] | (p[1] << 8) | (p[2] << 16) | ((uint32_t)p[3] << 24));
    memcpy(l->comp, p, sizeof l->comp); p += sizeof l->comp;
    memcpy(l->asked, p, sizeof l->asked);
}

/* The current side's ledger and the world, from RAM. */
static int current(CPUState *cpu, World *w, Ledger *m)
{
    static Raw r;
    if (!read_raw(cpu, &r)) return 0;
    split(&r, w, m);
    return 1;
}

/* Hot-seat's set-up: both sides start with the same ledger (the stock
 * blueprints, no goodwill). */
void ch_neg_setup(CPUState *cpu)
{
    static World w;
    g_have_other = current(cpu, &w, &g_other);
    if (g_have_other) {
        memset(g_other.asked, 0, sizeof g_other.asked);
        fprintf(stderr, "[negotiation] versus: the CPU's negotiation is off; each side has its own ledger\n");
    }
}

/* Hot-seat: the other side becomes "the player". */
void ch_neg_swap(CPUState *cpu)
{
    static Raw r;
    static World w;
    static Ledger m;
    if (!g_have_other || !read_raw(cpu, &r)) return;
    split(&r, &w, &m);
    merge(&r, &w, &g_other, &m);
    write_raw(cpu, &r);
    g_other = m;
}

/* ---- turn files ------------------------------------------------------- */

uint32_t ch_neg_size(void) { return NEG_SIZE; }

/* The section for the receiver: "NEG1", the world, the receiver's ledger,
 * then mine. Returns the bytes written (0 if the tables are not set up). */
uint32_t ch_neg_write(CPUState *cpu, uint8_t *p)
{
    static World w;
    static Ledger m;
    if (!g_have_other || !current(cpu, &w, &m)) return 0;
    memcpy(p, "NEG1", 4); p += 4;
    memcpy(p, w.parts, sizeof w.parts); p += sizeof w.parts;
    for (int i = 0; i < NSLOTS; i++, p += 2) set16(p, (uint16_t)w.slot_part[i]);
    put_ledger(p, &g_other); p += LEDGER_SIZE;
    put_ledger(p, &m);
    return NEG_SIZE;
}

/* Load a section into RAM. own = 0: the opponent's turn file (the first
 * ledger is mine); own = 1: my own file, turned back (the second ledger is
 * mine). Returns 1 if a section was loaded. */
int ch_neg_read(CPUState *cpu, const uint8_t *p, uint32_t n, int own)
{
    static Raw r;
    static World w;
    static Ledger a, b;
    if (n < NEG_SIZE || memcmp(p, "NEG1", 4) != 0 || !read_raw(cpu, &r)) return 0;
    p += 4;
    memcpy(w.parts, p, sizeof w.parts); p += sizeof w.parts;
    for (int i = 0; i < NSLOTS; i++, p += 2) w.slot_part[i] = (int16_t)le16(p);
    get_ledger(p, &a); p += LEDGER_SIZE;
    get_ledger(p, &b);
    const Ledger *mine = own ? &b : &a, *theirs = own ? &a : &b;
    merge(&r, &w, mine, theirs);
    write_raw(cpu, &r);
    g_other = *theirs;
    g_have_other = 1;
    return 1;
}

/* ---- End Turn --------------------------------------------------------- */

void ch_remote_lle_func_8004BBB8(CPUState *cpu);
void ch_remote_lle_func_800566F4(CPUState *cpu);
void ch_remote_lle_func_80049828(CPUState *cpu);
void ch_remote_lle_func_80049F44(CPUState *cpu);
void ch_remote_lle_func_8004A214(CPUState *cpu);
void ch_remote_lle_func_80049CA4(CPUState *cpu);

static int versus(void) { return ch_hotseat_active() && ch_hotseat_ready() && g_have_other; }

static Raw g_before;
static int g_pending;        /* 1: world half, 2: other half (put the world back) */

static int company_of(CPUState *cpu, int part)
{
    for (int c = 0; c < NCOMP; c++)
        for (int k = 0; k < 64; k++) {
            const int16_t v = (int16_t)cpu->read_half(COMPANY_LISTS + c * 138u + 10u + k * 2u);
            if (v < 0) break;
            if (v == part) return c;
        }
    return -1;
}

/* World half: blueprints finished just now that the opponent invested in
 * get the opponent's bonus, as the game gives it to the player
 * (0x8004BDD4): flag 0x0800, the company's flag bit 0, and a discount of
 * 10% (investment >= 80% of the price) or 20% (>= 160%). */
static void opponent_bonus(CPUState *cpu, const Raw *now)
{
    for (int s = 0; s < NSLOTS; s++) {
        const int16_t part = (int16_t)le16(g_before.slots[s]);
        if (part < 0 || part >= NPARTS) continue;
        if (!(le16(g_before.parts[part]) & F_IN_RD) || !(le16(now->parts[part]) & F_ON_SALE)) continue;
        uint8_t *e = g_other.parts[part];
        const uint16_t f = le16(e);
        const uint32_t inv = g_other.inv[s];
        if ((f & 0x2010u) != 0x2000u || inv == 0) continue;
        const uint32_t tenth = cpu->read_word(PARTS_TABLE + part * 36u + 0x1Cu) / 10u;
        set16(e, (uint16_t)(f | 0x0800u));
        e[3] = 0;
        if (inv >= tenth * 16u) e[2] |= 0x80;
        else if (inv >= tenth * 8u) e[2] |= 0x40;
        const int c = company_of(cpu, part);
        if (c >= 0) g_other.comp[c][6] |= 1;
        fprintf(stderr, "[negotiation] blueprint %d finished; the opponent's investment (%u) earns %s\n",
                part, inv, (e[2] & 0x80) ? "20% off" : (e[2] & 0x40) ? "10% off" : "no discount");
    }
}

/* Not the world half: put the world back. Blueprints that were still in
 * development keep their whole entry (a finish or delay rolled now did not
 * happen); finished ones keep this side's private changes (optimization
 * countdown, ammo shown or hidden). Goodwill and "asked" changes stay. */
static void put_world_back(CPUState *cpu, Raw *now)
{
    for (int i = 0; i < NPARTS; i++) {
        const uint16_t f0 = le16(g_before.parts[i]);
        if (!(f0 & F_ON_SALE)) {
            memcpy(now->parts[i], g_before.parts[i], 4);
        } else {
            const uint16_t f = (uint16_t)((f0 & ~F_PRIVATE) | (le16(now->parts[i]) & F_PRIVATE));
            set16(now->parts[i], f);
        }
    }
    memcpy(now->slots, g_before.slots, sizeof now->slots);
    write_raw(cpu, now);
}

/* End Turn's research and negotiation step. */
void func_8004BBB8(CPUState *cpu)
{
    if (cpu->pc == 0 && versus() && read_raw(cpu, &g_before))
        g_pending = ch_hotseat_world_half() ? 1 : 2;
    ch_remote_lle_func_8004BBB8(cpu);
}

/* Player production: End Turn calls it right after 0x8004BBB8. */
void func_800566F4(CPUState *cpu)
{
    if (cpu->pc == 0 && g_pending) {
        static Raw now;
        if (read_raw(cpu, &now)) {
            if (g_pending == 1) opponent_bonus(cpu, &now);
            else put_world_back(cpu, &now);
        }
        g_pending = 0;
    }
    ch_remote_lle_func_800566F4(cpu);
}

/* The CPU's negotiation: skipped in a versus game. They return what they
 * spent. */
static int skip(CPUState *cpu)
{
    if (cpu->pc != 0 || !versus()) return 0;
    cpu->gpr[2] = 0;
    cpu->pc = cpu->gpr[31];          /* jr $ra */
    return 1;
}
void func_80049828(CPUState *cpu) { if (!skip(cpu)) ch_remote_lle_func_80049828(cpu); }
void func_80049F44(CPUState *cpu) { if (!skip(cpu)) ch_remote_lle_func_80049F44(cpu); }
void func_8004A214(CPUState *cpu) { if (!skip(cpu)) ch_remote_lle_func_8004A214(cpu); }
void func_80049CA4(CPUState *cpu) { if (!skip(cpu)) ch_remote_lle_func_80049CA4(cpu); }
