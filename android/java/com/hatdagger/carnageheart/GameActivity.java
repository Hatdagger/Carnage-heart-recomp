package com.hatdagger.carnageheart;

import android.os.Bundle;
import android.system.ErrnoException;
import android.system.Os;

import java.io.File;

import org.libsdl.app.SDLActivity;

/** The game itself: SDL3 runs libmain.so (the recompiled game + runtime). */
public class GameActivity extends SDLActivity {

    /** Intent extra from SetupActivity's PLAY buttons. */
    static final String EXTRA_RENDERER = "renderer";
    /** OpenGL ES renderer (sharp internal resolution). The native side checks
     *  the GPU first and drops to software if it lacks what the renderer needs. */
    static final String RENDERER_GPU = "opengl";
    static final String RENDERER_SOFTWARE = "software";
    /** GPU internal resolution from the start screen: "2x", "3x" or "4x". */
    static final String EXTRA_SCALE = "scale";
    /** Online campaign game code (OnlineActivity's PLAY); absent: normal game. */
    static final String EXTRA_ONLINE_GAME = "online_game";

    /** True while the game is open (TurnCheckJob then posts no notification). */
    static volatile boolean running;

    @Override
    protected void onCreate(Bundle saved) {
        setOnlineSwitches();
        setSpeedBoost();
        running = true;
        super.onCreate(saved);
    }

    @Override
    protected void onDestroy() {
        running = false;
        super.onDestroy();
    }

    /** The relay switches src/turn_files.c and src/turn_relay.c read with
     *  getenv() (the desktop launcher sets the same ones). The game runs in
     *  this process, so they are set here, before SDL starts it; a normal
     *  game clears them. The turn folder is files/online-turns/<code>, the
     *  game's default (it runs with files/ as its folder). */
    private void setOnlineSwitches() {
        String code = getIntent() != null ? getIntent().getStringExtra(EXTRA_ONLINE_GAME) : null;
        Relay.Game g = code != null ? Relay.find(this, code) : null;
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

    /** SPEED BOOST on the start screen (default on): read by
     *  src/android/ch_android_boost.c and ch_android_main.c with getenv(). */
    private void setSpeedBoost() {
        boolean on = getSharedPreferences(SetupActivity.PREFS, MODE_PRIVATE)
            .getBoolean(SetupActivity.PREF_BOOST, true);
        try {
            Os.setenv("CH_ANDROID_BOOST", on ? "1" : "0", true);
        } catch (ErrnoException ignored) { }
    }

    private String scale() {
        String s = getIntent() != null ? getIntent().getStringExtra(EXTRA_SCALE) : null;
        return ("2x".equals(s) || "4x".equals(s)) ? s : "3x";
    }

    private String renderer() {
        String r = getIntent() != null ? getIntent().getStringExtra(EXTRA_RENDERER) : null;
        return RENDERER_SOFTWARE.equals(r) ? RENDERER_SOFTWARE : RENDERER_GPU;
    }

    @Override
    protected String[] getLibraries() {
        // SDL3 is linked statically into libmain.so.
        return new String[] { "main" };
    }

    @Override
    protected String[] getArguments() {
        File cue = GameFiles.findDisc(GameFiles.discDir(this));
        if (cue == null) {
            return new String[] { "--no-launcher", "--renderer", renderer(),
                                  "--android-scale", scale() };
        }
        return new String[] {
            "--no-launcher",
            "--renderer", renderer(),
            "--android-scale", scale(),
            "--disc", cue.getAbsolutePath(),
        };
    }
}
