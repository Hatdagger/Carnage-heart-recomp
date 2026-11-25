/* Carnage Heart: tutorial picture capture (see src/tutorial_capture.c). */
#pragma once
#include <stdint.h>
#include "cpu_state.h"

/* 1 when CH_TUTORIAL_CAPTURE / CH_TUTORIAL_OUT switched capture on. */
int ch_capture_enabled(void);
/* Once per game PadRead, instead of the controller. screen_ra: the menu
 * screen on show (0 = none). Writes the button word, mouse mode and cursor.
 * Returns 0 when capture is off (nothing written), else 1. */
int ch_capture_frame(CPUState *cpu, uint32_t screen_ra);
