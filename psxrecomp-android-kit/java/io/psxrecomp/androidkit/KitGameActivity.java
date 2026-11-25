package io.psxrecomp.androidkit;

import android.os.Bundle;

import java.io.File;

import org.libsdl.app.SDLActivity;

/** The game itself: SDL3 runs libmain.so (the recompiled game + runtime). */
public class KitGameActivity extends SDLActivity {

    /** Intent extra from the start screen's PLAY buttons. */
    public static final String EXTRA_RENDERER = "renderer";
    /** OpenGL ES renderer (sharp internal resolution). The native side checks
     *  the GPU first and drops to software if it lacks what the renderer needs. */
    public static final String RENDERER_GPU = "opengl";
    public static final String RENDERER_SOFTWARE = "software";
    /** GPU internal resolution from the start screen: "2x", "3x" or "4x". */
    public static final String EXTRA_SCALE = "scale";

    /** True while the game is open (a game's background job can check it). */
    public static volatile boolean running;

    @Override
    protected void onCreate(Bundle saved) {
        KitHooks.Loader.get().beforeGameStart(this, getIntent());
        running = true;
        super.onCreate(saved);
    }

    @Override
    protected void onDestroy() {
        running = false;
        KitHooks.Loader.get().onGameDestroyed(this);
        super.onDestroy();
    }

    private String scale() {
        String s = getIntent() != null ? getIntent().getStringExtra(EXTRA_SCALE) : null;
        return KitSetupActivity.validScale(s) ? s : KitConfig.DEFAULT_SCALE;
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
        File cue = KitFiles.findDisc(KitFiles.discDir(this));
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
