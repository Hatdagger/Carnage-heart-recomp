# android_game.json

One file in the game's root. Only `package` and `app_name` are required; the
rest have defaults made from `app_name`. Keys starting with `_` are ignored.

## Identity

| Key | Default | What |
|---|---|---|
| `package` | (required) | Android package name, e.g. `com.hatdagger.carnageheart`. Keep it the same forever: a new one is a different app. |
| `app_name` | (required) | Name under the icon and in logs and messages. |
| `title` | `app_name` in capitals | Big title on the start screen. |
| `exe_name` | `psx_game` | The PC game's executable name. The runtime finds its files relative to it, so use the name the PC build has (e.g. `CarnageHeart_Recompiled`). |
| `generated_check` | none | A file under the game root that exists once the game C has been generated from the disc (e.g. `generated/SCUS_946.04_dispatch.c`). The builders refuse to run without it. |

## Logs and folders

| Key | Default | What |
|---|---|---|
| `log_tag` | `PSXRecomp` | Logcat tag. |
| `log_name` | `game` | Log file name on the device (`files/<log_name>.log`) and the Downloads copy (`<log_name>-log-<time>.txt`). |
| `data_dir_name` | `app_name` without spaces | Per-user folder for the Android tools and the signing key: `%LOCALAPPDATA%\<name>` on Windows, `~/.local/share/<name in lower case>` on Linux. |

## Look and play

| Key | Default | What |
|---|---|---|
| `default_scale` | `3x` | GPU internal resolution before the player picks one with SHARPNESS: `2x`, `3x` or `4x`. |
| `accent` / `background` | green on dark green | Start screen colours (`#RRGGBB`). |
| `disc_prompt` | generic text | What the start screen says when it asks for the disc. Say how many .bin files there are. |
| `launcher_alias` | none | Only for a game whose app existed before it used the kit: the old start-screen class (e.g. `.SetupActivity`), so the home-screen icon of an installed app keeps working after the update. |
| `res_dir` | `android/res` | Android resources folder; needs `mipmap-*/ic_launcher.png`. |

## Signing and output

| Key | Default | What |
|---|---|---|
| `apk_name` | `<AppName>.apk` | Output file in the game root. |
| `keystore_name` | `<appname>.keystore` | Signing key file, made once and kept. Keep it: an APK signed with another key won't install over the old app. |
| `key_alias` / `key_pass` | `<appname>` | Personal side-load key, not a secret. |
| `cert_name` | `<App name> Recompiled (personal build)` | Name inside the certificate. |

## Game files

| Key | Default | What |
|---|---|---|
| `game_files` | `game.toml`, `game_options.toml`, `bios`, `mods`, `assets` | Files and folders the CMake build stages beside the library that go into the APK and are unpacked on the device. |

## Game's own extras

For things only one game has (Carnage Heart's online campaign, for example).

| Key | What |
|---|---|
| `java_dir` | Folder with the game's own Java sources (any package). Compiled with the kit's. |
| `java_hooks` | A class implementing `io.psxrecomp.androidkit.KitHooks` (`onReady`, `beforeGameStart`, `onGameDestroyed`). `.Name` means `<package>.Name`. |
| `extra_buttons` | Start screen buttons that open the game's own activities: `[{"label": "ONLINE CAMPAIGN", "activity": ".OnlineActivity"}]`. |
| `permissions` | Extra Android permissions, e.g. `"android.permission.INTERNET"`. |
| `manifest_application_xml` | A file whose text goes inside `<application>`: the game's own activities, services and receivers. `@PACKAGE@` is replaced by `package`. |

Native side:

- C files the game adds: `psxak_finish_targets(SOURCES src/foo_android.c ...)` in CMake.
- `void psxak_game_start(void)`: runs on the SDL thread just before the game's main().
- `void psxak_draw_overlay_sdl(SDL_Renderer *r)`: draws on top of each finished frame with the software renderer (e.g. a touch menu). With the OpenGL renderer use the runtime's HUD hook (`psx_host_set_hud_draw_cb`).
