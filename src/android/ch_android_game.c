/* Carnage Heart's additions to the PSXRecomp Android kit's start-up
 * (psxrecomp-android-kit/native/psxak_main.c calls psxak_game_start just
 * before the game's main()). */
#include <stdio.h>
#include <stdlib.h>

void ch_android_relay_init(void); /* ch_android_relay.c */

void psxak_game_start(void)
{
    /* Online campaign: the relay requests go through Relay.java
     * (ch_android_relay.c); look the class up here, on a Java thread. */
    const char *game = getenv("CH_TURN_GAME");
    if (game && *game) {
        ch_android_relay_init();
        printf("Carnage Heart (Android): online game %s, player %s\n", game,
               getenv("CH_TURN_ROLE") ? getenv("CH_TURN_ROLE") : "?");
    }
}
