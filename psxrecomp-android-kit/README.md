# PSXRecomp Android kit

Builds an Android app (arm64 APK) for a [PSXRecomp](https://github.com/mstan/psxrecomp)
game project, from the player's own disc, on the player's PC. It sits beside
psxrecomp and doesn't change it, so psxrecomp updates come in as usual.

Made from the Carnage Heart ReComp Android port, which runs at 60 fps on a
Lenovo Legion Tab Y700 Gen 2 (Snapdragon 8+ Gen 1) with the OpenGL ES renderer
at 3x.

## What's in it

| Folder | What |
|---|---|
| `cmake/psxak.cmake` | Turns the game into `libmain.so` for SDLActivity, turns off desktop-only parts (launcher, wizard, netplay, Vulkan, OpenXR, rewind, PGXP), adds the native files and link wraps. |
| `native/` | SDL_main wrapper (game folder, log to logcat + file, GPU probe, renderer and sharpness choice), OpenGL ES 3.2 bridge for the runtime's GL 3.3 renderer (shader translation, BGRA formats), `getcontext` & co. for Bionic, frame-time hint, the `android watch` diagnostic line. |
| `java/` | Start screen (disc picker, PLAY, SHARPNESS 2x/3x/4x, PLAY (SOFTWARE RENDERER), SHARE LAST LOG, SAVE LOG TO DOWNLOADS) and the game activity. |
| `templates/` | AndroidManifest template and the `Build Android App.bat` / `build-android-app.sh` launchers for a setup kit. |
| `scripts/build_apk.py` | SDL3 (static) → libmain.so → Java → resources → APK → zipalign → sign. |
| `scripts/build_android_app.py` | One-click builder for players: downloads the pinned Android tools once (NDK r27d, build-tools 35, platform 35, JDK 21; ~1.1 GB), then runs `build_apk.py`. |
| `scripts/check_disc.py` | First step for a new game: lists the disc's programs and likely overlay files. |
| `docs/SETTINGS.md` | Every key of `android_game.json`. |

## Before you start: does the game load code mid-game?

Android can't compile code while the game runs, so all game code must be in
the APK. Run:

```
python3 psxrecomp-android-kit/scripts/check_disc.py path/to/game.cue
```

- **One PS-X EXE, no overlays** (Carnage Heart): everything compiles ahead of
  time. Easy.
- **More programs or overlays**: recompile every program, and play every part
  of the game on the PC build so PSXRecomp records the overlays it loads.
  Code the playthrough missed falls back to the slow interpreter on the device.

## Adding it to a game

1. Add the kit as a submodule beside psxrecomp:
   ```
   git submodule add https://github.com/Hatdagger/psxrecomp-android-kit
   ```
2. Copy `android_game.example.json` to the game root as `android_game.json` and fill it in
   (see `docs/SETTINGS.md`). Put icons in `android/res/mipmap-*/ic_launcher.png`.
3. In the game's `CMakeLists.txt`, before `runtime.cmake` is included:
   ```cmake
   if(ANDROID)
       include("${CMAKE_CURRENT_SOURCE_DIR}/psxrecomp-android-kit/cmake/psxak.cmake")
   endif()
   ```
   and at the end, after the game target exists:
   ```cmake
   if(PSXAK_ANDROID)
       psxak_finish_targets()
   endif()
   ```
4. Ship the kit folder in the setup kit, and copy `templates/kit/Build Android App.bat`
   and `build-android-app.sh` to the setup kit's root.
5. Build: `Build Android App.bat` on the player's PC, or for development
   `python3 psxrecomp-android-kit/scripts/build_apk.py --ndk ... --platform-jar ... --build-tools ...`.

## Touch

The kit gives touch to SDL, which the runtime sees as mouse/finger events.
A game-specific touch menu (Carnage Heart's long-press button menu,
`src/pointer_menu.c` there) stays in the game: on the software renderer it
draws through `psxak_draw_overlay_sdl()`, on the GPU renderer through the
runtime's HUD hook.

## What the APK is

It contains code made from the player's own disc: for their own devices only.
The kit and game repos never hold game code.

Requirements on the device: Android 10+ (API 29), arm64. The GPU renderer
needs OpenGL ES 3.1+ with `GL_EXT_blend_func_extended`; otherwise PLAY uses
the software renderer.
