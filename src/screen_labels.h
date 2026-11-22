/* Carnage Heart: per-screen button labels for the right-click menu.
 * See src/screen_labels.c. */
#pragma once
#include <stdint.h>

typedef struct ChScreenItem { const char *label; uint32_t bit; } ChScreenItem;

/* ra: return address of the screen's latest HotspotNavigate /
 * HotspotDrawHitTest call. Fills the buttons that do something on that screen
 * (besides Cross and Circle) and returns how many, or -1 when ra is not a
 * known menu screen. */
int ch_screen_items(uint32_t ra, ChScreenItem *out, int max);
