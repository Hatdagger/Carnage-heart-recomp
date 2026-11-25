package com.hatdagger.carnageheart;

import android.app.Activity;
import android.content.Intent;
import android.system.ErrnoException;
import android.system.Os;

import io.psxrecomp.androidkit.KitHooks;

/**
 * Carnage Heart's additions to the PSXRecomp Android kit's start screen and
 * game activity (android_game.json "java_hooks"): the online campaign.
 */
public class CarnageHooks implements KitHooks {
    /** Online campaign game code (OnlineActivity's PLAY); absent: normal game. */
    static final String EXTRA_ONLINE_GAME = "online_game";

    @Override
    public void onReady(Activity setup) {
        if (!Relay.load(setup).isEmpty()) TurnCheckJob.schedule(setup);
    }

    /** The relay switches src/turn_files.c and src/turn_relay.c read with
     *  getenv() (the desktop launcher sets the same ones). The game runs in
     *  this process, so they are set here, before SDL starts it; a normal
     *  game clears them. The turn folder is files/online-turns/<code>, the
     *  game's default (it runs with files/ as its folder). */
    @Override
    public void beforeGameStart(Activity game, Intent intent) {
        String code = intent != null ? intent.getStringExtra(EXTRA_ONLINE_GAME) : null;
        Relay.Game g = code != null ? Relay.find(game, code) : null;
        try {
            Os.unsetenv("CH_TURN_DIR");
            if (g == null) {
                Os.unsetenv("CH_TURN_RELAY");
                Os.unsetenv("CH_TURN_GAME");
                Os.unsetenv("CH_TURN_KEY");
                Os.unsetenv("CH_TURN_ROLE");
                return;
            }
            Os.setenv("CH_TURN_RELAY", Relay.base(g.relay), true);
            Os.setenv("CH_TURN_GAME", g.code, true);
            Os.setenv("CH_TURN_KEY", g.key, true);
            Os.setenv("CH_TURN_ROLE", String.valueOf(g.role), true);
        } catch (ErrnoException ignored) { }
    }
}
