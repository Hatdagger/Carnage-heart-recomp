/* Carnage Heart (USA): which pad buttons do something on each menu screen,
 * for the labels in the right-click menu (src/pointer_menu.c).
 *
 * A menu screen is a game function that calls HotspotNavigate (0x8003E89C) or
 * HotspotDrawHitTest (0x8003EEC4) every frame; mouse_pad_hle.c notes the
 * return address of those calls, which falls inside the screen's function.
 *
 * The table was made by scanning the USA executable (tools/scan_screens.py):
 * for each screen, the buttons its code (and the helper routines it calls that
 * are not screens of their own) tests on the button word 0x800E0134.
 * Left out on purpose:
 *   - Cross and Circle (left click and Back already do those);
 *   - Triangle where it only switches the D-pad between "jump between boxes"
 *     and "free cursor" while held;
 *   - R1/R2 and L2 where they are only held together with the D-pad (scroll);
 *   - Select checked together with Circle, Start together with Cross (just a
 *     second Back / OK).
 * Hints, checked in the sandbox:
 *   H_QUIT      Select opens the Quit Game / Cancel box (0x8001B0C0);
 *   H_NEXTBASE  L2 steps to your next base (0x800DF404 over 0x800E0154);
 *   H_NEXTUNIT  L2 steps to your next unit (0x800DF408 over 0x800E017C);
 *   H_CHIPS     R2 opens the chip list (0x8004591C) on the chip program grid
 *               (screen 0x80041188; its helper 0x80047948 also scrolls the
 *               grid while R1 is held, which the on-screen arrows do too);
 *   H_BATTLE    a battle (no hotspots; the pad code notes the battle viewer
 *               0x8001FB04 and the buttons are read by 0x800220F4):
 *               L2 = camera to the next OKE, Select = display settings
 *               (0x80023F54 / 0x80024564), Start = every OKE's status in a
 *               test battle, next OKE otherwise; with two players watching
 *               L2 and Start don't switch OKE. In mouse mode (0x800DF358 = 1,
 *               always so while the pointer is in use) the game ignores
 *               Select and Start there and takes Circle and Cross instead, as
 *               in EZ: so the menu entries press those. */
#include "screen_labels.h"

#define B_L2     0x0001u
#define B_R2     0x0002u
#define B_SELECT 0x0100u
#define B_START  0x0800u
#define B_TRI    0x0010u
#define B_CIRCLE 0x0020u
#define B_CROSS  0x0040u

enum { H_QUIT = 1, H_NEXTBASE = 2, H_NEXTUNIT = 4, H_CHIPS = 8, H_BATTLE = 16 };

typedef struct { uint32_t start, end; uint16_t bits; uint8_t hints; } Screen;

static const Screen k_screens[] = {
    { 0x8001B0C0u, 0x8001B4CCu, 0x000u, 0 },
    { 0x8001FB04u, 0x8001FB08u, 0x901u, H_BATTLE },
    { 0x80023F54u, 0x80024564u, 0x100u, 0 },
    { 0x80024564u, 0x80024A7Cu, 0x100u, 0 },
    { 0x80024DE4u, 0x80025204u, 0x000u, 0 },
    { 0x80037B78u, 0x80037EC8u, 0x100u, H_QUIT },
    { 0x80038414u, 0x80038644u, 0x100u, H_QUIT },
    { 0x80038BA0u, 0x80038E7Cu, 0x100u, H_QUIT },
    { 0x80039424u, 0x80039768u, 0x101u, H_QUIT|H_NEXTBASE },
    { 0x8003A37Cu, 0x8003A6A0u, 0x101u, H_QUIT|H_NEXTUNIT },
    { 0x8003AF54u, 0x8003B2E0u, 0x100u, H_QUIT },
    { 0x8003B8A4u, 0x8003BC80u, 0x100u, H_QUIT },
    { 0x8003BC80u, 0x8003C350u, 0x100u, H_QUIT },
    { 0x8003C6D4u, 0x8003D218u, 0x100u, H_QUIT },
    { 0x80041188u, 0x800415DCu, 0x102u, H_QUIT|H_CHIPS },
    { 0x800426FCu, 0x80042A40u, 0x100u, 0 },
    { 0x80042FF0u, 0x800433E0u, 0x100u, H_QUIT },
    { 0x800440F0u, 0x80044300u, 0x100u, H_QUIT },
    { 0x8004591Cu, 0x80045C68u, 0x100u, H_QUIT },
    { 0x8004CB60u, 0x8004D200u, 0x100u, H_QUIT },
    { 0x8004D470u, 0x8004DC54u, 0x100u, H_QUIT },
    { 0x8004E37Cu, 0x8004EAB0u, 0x000u, 0 },
    { 0x8004EAB0u, 0x8004F370u, 0x100u, H_QUIT },
    { 0x8004FE04u, 0x800505C0u, 0x100u, H_QUIT },
    { 0x800508F0u, 0x80051344u, 0x100u, H_QUIT },
    { 0x800516E0u, 0x80051E94u, 0x100u, H_QUIT },
    { 0x800525D4u, 0x80052D80u, 0x100u, H_QUIT },
    { 0x80052F6Cu, 0x8005374Cu, 0x100u, H_QUIT },
    { 0x80056DA4u, 0x800571B4u, 0x100u, H_QUIT },
    { 0x8005785Cu, 0x800581DCu, 0x100u, H_QUIT },
    { 0x80058438u, 0x80058DECu, 0x100u, H_QUIT },
    { 0x80058DECu, 0x800590E4u, 0x100u, H_QUIT },
    { 0x800593DCu, 0x800597F0u, 0x100u, H_QUIT },
    { 0x8005A0FCu, 0x8005A8A4u, 0x100u, H_QUIT },
    { 0x8005A8A4u, 0x8005AC38u, 0x100u, H_QUIT },
    { 0x8005D700u, 0x8005DFB8u, 0x100u, H_QUIT },
    { 0x8005EFA0u, 0x8005F52Cu, 0x100u, H_QUIT },
    { 0x8005F764u, 0x800605D8u, 0x100u, H_QUIT },
    { 0x8006145Cu, 0x800617B0u, 0x100u, H_QUIT },
    { 0x80061D18u, 0x8006256Cu, 0x100u, H_QUIT },
    { 0x8006256Cu, 0x80062974u, 0x000u, 0 },
    { 0x800638B8u, 0x80063F70u, 0x100u, H_QUIT },
    { 0x8006464Cu, 0x80064C54u, 0x100u, H_QUIT },
    { 0x80064C54u, 0x8006549Cu, 0x100u, H_QUIT },
    { 0x8006A944u, 0x8006AD98u, 0x100u, H_QUIT },
    { 0x8006C12Cu, 0x8006C67Cu, 0x100u, H_QUIT },
    { 0x8006C67Cu, 0x8006D294u, 0x100u, H_QUIT },
    { 0x8006D294u, 0x8006D7E4u, 0x100u, H_QUIT },
    { 0x8006DF94u, 0x8006E3BCu, 0x100u, H_QUIT },
    { 0x8006F120u, 0x800706ACu, 0x100u, H_QUIT },
    { 0x80072F24u, 0x80073828u, 0x100u, H_QUIT },
    { 0x80073F5Cu, 0x80074648u, 0x100u, H_QUIT },
    { 0x80074D7Cu, 0x80075A80u, 0x900u, H_QUIT },
    { 0x800769A4u, 0x80076D08u, 0x101u, H_QUIT|H_NEXTUNIT },
    { 0x80077F4Cu, 0x800784D4u, 0x100u, H_QUIT },
    { 0x8007863Cu, 0x8007958Cu, 0x000u, 0 },
    { 0x8007958Cu, 0x8007A95Cu, 0x000u, 0 },
    { 0x8007A95Cu, 0x8007AB04u, 0x000u, 0 },
    { 0x8007AB04u, 0x8007ACACu, 0x000u, 0 },
    { 0x8007ACACu, 0x8007B22Cu, 0x000u, 0 },
    { 0x8007C6F4u, 0x8007CFB0u, 0x000u, 0 },
    { 0x8007E3DCu, 0x8007E798u, 0x000u, 0 },
    { 0x8007ED0Cu, 0x8007F540u, 0x000u, 0 },
    { 0x8007F540u, 0x8007FF94u, 0x000u, 0 },
    { 0x80080034u, 0x80080384u, 0x000u, 0 },
    { 0x80080D10u, 0x80081AFCu, 0x100u, H_QUIT },
    { 0x80081EA4u, 0x800825C4u, 0x100u, H_QUIT },
    { 0x80084100u, 0x800845F4u, 0x000u, 0 },
    { 0x800845F4u, 0x80084B90u, 0x000u, 0 },
    { 0x800859A4u, 0x80086328u, 0x100u, H_QUIT },
    { 0x80086328u, 0x80086CACu, 0x100u, H_QUIT },
    { 0x80086DA0u, 0x800874CCu, 0x100u, H_QUIT },
    { 0x8008BA6Cu, 0x8008BD7Cu, 0x100u, H_QUIT },
};
#define N_SCREENS ((int)(sizeof k_screens / sizeof k_screens[0]))

static volatile int s_battle_mode, s_battle_pads;
void ch_screen_set_battle(int mode, int pads) { s_battle_mode = mode; s_battle_pads = pads; }

int ch_screen_items(uint32_t ra, ChScreenItem *out, int max) {
    int lo = 0, hi = N_SCREENS - 1;
    const Screen *s = 0;
    while (lo <= hi) {                      /* table is sorted by start */
        const int mid = (lo + hi) / 2;
        if (ra < k_screens[mid].start) hi = mid - 1;
        else if (ra >= k_screens[mid].end) lo = mid + 1;
        else { s = &k_screens[mid]; break; }
    }
    if (!s) return -1;
    int n = 0;
    if (s->hints & H_BATTLE) {
        const int one = s_battle_pads < 2;   /* a single player watching */
        if (one && n < max) out[n++] = (ChScreenItem){ "L2: NEXT OKE", B_L2 };
        if (n < max) out[n++] = (ChScreenItem){ "SETTINGS", B_CIRCLE };
        if (s_battle_mode == 0 && n < max) out[n++] = (ChScreenItem){ "OKE STATUS", B_CROSS };
        else if (one && n < max)           out[n++] = (ChScreenItem){ "NEXT OKE", B_CROSS };
        return n;
    }
    if ((s->bits & B_R2) && n < max)
        out[n++] = (ChScreenItem){ (s->hints & H_CHIPS) ? "R2: CHIP LIST" : "R2", B_R2 };
    if ((s->bits & B_SELECT) && n < max)
        out[n++] = (ChScreenItem){ (s->hints & H_QUIT) ? "SELECT: QUIT GAME" : "SELECT", B_SELECT };
    if ((s->bits & B_L2) && n < max)
        out[n++] = (ChScreenItem){ (s->hints & H_NEXTBASE) ? "L2: NEXT BASE" :
                                   (s->hints & H_NEXTUNIT) ? "L2: NEXT UNIT" : "L2", B_L2 };
    if ((s->bits & B_TRI) && n < max)   out[n++] = (ChScreenItem){ "TRIANGLE", B_TRI };
    if ((s->bits & B_START) && n < max) out[n++] = (ChScreenItem){ "START", B_START };
    return n;
}
