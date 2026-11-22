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

/* 1 when pointer mode may run this frame (not in netplay, not while the
 * PS1 Mouse has the pointer captured, not turned off with CH_POINTER=0). */
int  ch_pointer_enabled(void);
/* Once per game PadRead, on the main thread. */
void ch_pointer_frame(ChPointerFrame *out);
