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

/* Pseudo return address for "a battle is on show" (the battle viewer
 * 0x8001FB04 ran; the battle itself has no hotspot boxes). */
#define CH_SCREEN_BATTLE 0x8001FB04u

/* Battle type, read from RAM while a battle plays: mode 0 = test battle
 * (Start shows every OKE's status), else campaign/VS (Start switches OKE);
 * pads 2 = two players watching (L2 / Start don't switch OKE). */
void ch_screen_set_battle(int mode, int pads);
