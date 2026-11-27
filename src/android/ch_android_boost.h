/* Android "speed boost" (ch_android_boost.c). */
#ifndef CH_ANDROID_BOOST_H
#define CH_ANDROID_BOOST_H

/* 1 unless CH_ANDROID_BOOST=0 (the start screen's SPEED BOOST: OFF). */
int ch_android_boost_enabled(void);

/* Call on the game thread before the runtime starts. */
void ch_android_boost_start(void);

/* Which cores to pin to. max_khz[c] is core c's top clock (<= 0 unknown),
 * allowed[c] is 1 if the app may use core c. Sets fast[c] = 1 for the allowed
 * cores with the highest top clock and returns how many there are. Returns 0
 * (and leaves fast[] all 0) when pinning would not help or is unsafe: fewer
 * than two allowed cores, an allowed core with an unknown clock, or every
 * allowed core the same speed. */
int ch_boost_pick_fast(const long *max_khz, const unsigned char *allowed, int n,
                       unsigned char *fast);

#endif
