package com.hatdagger.carnageheart;

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
