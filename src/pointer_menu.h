/* Carnage Heart: window pointer, touch and right-click button menu.
 *
 * Lets a normal (uncaptured) mouse or a touch screen drive the game's own
 * mouse cursor while the gamepad / keyboard stays the port-1 controller, so
 * both work at once. See src/pointer_menu.c. */
#pragma once
#include <stdint.h>

typedef struct ChPointerFrame {
    int      activity;  /* pointer moved, clicked or a menu entry fired */
    int      x, y;      /* game cursor position (512 x 240 screen) */
    uint32_t buttons;   /* game button bits to OR into the pad word */
} ChPointerFrame;

/* Once per game PadRead, before ch_pointer_frame: 1 when the screen on show
 * is a list with one-row up/down arrow boxes (found by the hotspot routine's
 * wrapper in mouse_pad_hle.c), so the wheel can press them. */
void ch_pointer_set_list(int on);
/* For the hotspot routine's wrapper: -1 when this frame's press is a wheel
 * notch up (report the list's up arrow as the box under the cursor), +1 for
 * down, 0 for none. */
int  ch_pointer_list_step(void);

/* 1 when pointer mode may run this frame (not in netplay, not while the
 * PS1 Mouse has the pointer captured, not turned off with CH_POINTER=0). */
int  ch_pointer_enabled(void);
/* Once per game PadRead, on the main thread. */
void ch_pointer_frame(ChPointerFrame *out);
/* Once per game PadRead, before ch_pointer_frame: return address of the menu
 * screen on show (see src/screen_labels.c), 0 when none is. */
void ch_pointer_set_screen(uint32_t ra);
