# Android build

Builds an arm64 APK of Carnage Heart from **your own disc**. The APK holds
code made from your disc, so it is for your own devices only.

The Android work is done by the **PSXRecomp Android kit**
(`psxrecomp-android-kit/`, a submodule shared with other PSXRecomp games).
This game's settings are in `android_game.json`; what is left here is only
Carnage Heart's own: icons, the online campaign screens and the touch menu.

Players don't run this by hand: the setup kit's "Build Android App" step (and
the launcher's BUILD ANDROID APP row) downloads the tools once and runs it.
For developers:

```sh
git submodule update --init psxrecomp-android-kit
./psxrecomp/tools/ci/build_emitters.sh
python3 psxrecomp/psxrecomp_cli.py generate --config game.toml --project-root . --disc "/path/to/Carnage Heart (USA).cue"
python3 scripts/android/build_apk.py \
  --ndk <Android NDK r27+> \
  --platform-jar <sdk>/platforms/android-35/android.jar \
  --build-tools <sdk>/build-tools/35.0.0
# -> CarnageHeart.apk (signed with android-signing/carnageheart.keystore,
#    made on first use; keep it so updates install over the old app)
```

Needs cmake, ninja and a JDK 17+ (javac, java, keytool) on PATH or passed
with `--cmake`, `--ninja`, `--jdk`. psxrecomp/third_party must hold the
vendored SDL3/zlib/libwebp archives (the setup kit ships them;
`psxrecomp/tools/ci/vendor_deps.sh` fetches them).

## What's where
- **The kit** (`psxrecomp-android-kit/`, see its README): the game becomes
  `libmain.so` for SDLActivity; desktop-only parts off; the SDL_main wrapper
  (game folder, logs to logcat and `files/carnageheart.log`, GPU probe,
  sharpness); the OpenGL ES bridge; getcontext/swapcontext for Bionic; the
  start screen (disc picker, PLAY, SHARPNESS, software renderer, logs) and
  the game activity; the APK and one-click builders.
- `android_game.json`: package, names, colours, disc prompt, signing key name,
  the ONLINE CAMPAIGN button, extra permissions.
- `android/res/`: icons. `android/manifest_application.xml`: the online
  screen and the turn check service (added to the kit's manifest).
- `scripts/android/build_apk.py`, `build_android_app.py`: thin wrappers that
  run the kit's builders with this game's settings (the launcher and the
  kit launchers in `scripts/android/kit/` call these paths).
- `src/android/ch_android_game.c`: start-up hook (`psxak_game_start`) for the
  online relay. `src/pointer_menu.c` draws the touch menu through the kit's
  `psxak_draw_overlay_sdl()` on the software renderer.
- Online campaign (`-DCH_REMOTE_BATTLE=ON`, on here):
  `OnlineActivity` is the Online screen (games, NEW GAME, JOIN A GAME, PLAY,
  SHARE CODE, FORGET); `CarnageHooks` (the kit's `KitHooks`) sets the
  `CH_TURN_*` switches with `Os.setenv` before the game starts and schedules
  the turn check; `Relay.java` talks to the relay and also makes the game's
  own relay requests (Android has no curl; `src/android/ch_android_relay.c`
  calls it through JNI); `TurnCheckJob` checks about every 15 minutes and
  posts "it's your turn". Games and turn files live in `files/online-turns/`
  as on the desktop. PC test of the relay code:
  `tools/research/android_relay_test/run.sh`.
- Minimum Android 10 (API 29), target API 35, 16 KB page aligned.
