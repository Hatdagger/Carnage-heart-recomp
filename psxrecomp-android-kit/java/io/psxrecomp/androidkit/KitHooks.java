package io.psxrecomp.androidkit;

import android.app.Activity;
import android.content.Intent;

/**
 * Optional game hooks. A game that needs more than the kit's start screen and
 * game activity names a class implementing this in android_game.json
 * ("java_hooks"); it must have a public no-argument constructor. Every method
 * has a default that does nothing.
 */
public interface KitHooks {
    /** The start screen is ready (game files unpacked, disc in place). */
    default void onReady(Activity setup) {}

    /** The game activity is starting, before SDL loads the game. Runs on the
     *  UI thread; a good place to set environment switches with Os.setenv. */
    default void beforeGameStart(Activity game, Intent intent) {}

    /** The game activity is going away. */
    default void onGameDestroyed(Activity game) {}

    /** Loads the hooks class named in KitConfig.HOOKS, or a do-nothing one. */
    final class Loader {
        private static KitHooks hooks;

        private Loader() {}

        public static synchronized KitHooks get() {
            if (hooks != null) return hooks;
            if (KitConfig.HOOKS != null && !KitConfig.HOOKS.isEmpty()) {
                try {
                    hooks = (KitHooks) Class.forName(KitConfig.HOOKS)
                        .getDeclaredConstructor().newInstance();
                } catch (ReflectiveOperationException | ClassCastException e) {
                    android.util.Log.e(KitConfig.LOG_TAG, "hooks class " + KitConfig.HOOKS
                        + " could not be loaded", e);
                }
            }
            if (hooks == null) hooks = new KitHooks() {};
            return hooks;
        }
    }
}
