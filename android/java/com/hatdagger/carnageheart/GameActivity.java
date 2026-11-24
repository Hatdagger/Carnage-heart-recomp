package com.hatdagger.carnageheart;

import java.io.File;

import org.libsdl.app.SDLActivity;

/** The game itself: SDL3 runs libmain.so (the recompiled game + runtime). */
public class GameActivity extends SDLActivity {

    @Override
    protected String[] getLibraries() {
        // SDL3 is linked statically into libmain.so.
        return new String[] { "main" };
    }

    @Override
    protected String[] getArguments() {
        File cue = GameFiles.findDisc(GameFiles.discDir(this));
        if (cue == null) {
            return new String[] { "--no-launcher", "--renderer", "software" };
        }
        // Software renderer first: it presents through SDL_Renderer (GLES).
        return new String[] {
            "--no-launcher",
            "--renderer", "software",
            "--disc", cue.getAbsolutePath(),
        };
    }
}
