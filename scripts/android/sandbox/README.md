# APK build without the Android NDK (build sandbox)

For the Claude build sandbox, which cannot download the NDK from Google.
Players never use this: their setup kit downloads the real NDK.

1. Clone the `android` branch. `gh` must be logged in with access to this
   repo (the devkit is a private draft release; use the REST API, GraphQL
   may be blocked).
2. `scripts/android/sandbox/prepare_sandbox.sh /tmp/ch-android` and run the
   `export` line it prints.
3. Unpack the disc. If it is a .7z and no 7z tool is installed:
   `python3 -I scripts/android/sandbox/un7z.py Carnage_Heart__USA_.7z /tmp/disc`
   (Track 1 md5 6830aa39ea95f4c380f8a3f49136fee7).
4. Generate the game C from the disc:
   `./psxrecomp/tools/ci/build_emitters.sh --jobs 4`
   `python3 psxrecomp/psxrecomp_cli.py generate --config game.toml --project-root . --disc "<the .cue>"`
5. Build:
   `python3 scripts/android/build_apk.py --toolchain-file scripts/android/sandbox/android-devkit.cmake --platform-jar $ANDROID_DEVKIT/platform/android.jar --build-tools $ANDROID_DEVKIT/build-tools --jobs 4`

Signing: build_apk.py makes `android-signing/carnageheart.keystore` on first
use (git-ignored). A new sandbox makes a new key, so the device must
uninstall the old test APK first unless the same keystore is supplied.
