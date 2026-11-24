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

   Pass `--toolchain-file` as an absolute path ($PWD/...): SDL3 is configured
   from another directory. If psxrecomp/ was filled with
   `git submodule update` instead of from the kit, also run
   `git -C psxrecomp submodule update --init --depth 1` and copy the kit's
   `psxrecomp/third_party/*.tar.gz` into `psxrecomp/third_party/`.

Signing: the owner keeps the test key `carnageheart.keystore` (made 24 Nov
2026, cert SHA-256 19:6D:C3:EC:...:C9:98). Ask them to attach it and pass
`--keystore <path>`, so the APK installs over the previous test. Without it
build_apk.py makes a new key and the device must uninstall first.

GLES renderer check without a device: `tools/research/gles_shader_check/`
(shader compile/link on Mesa, and a headless Linux run through the same
GLES bridge with -DCH_GLES_DESKTOP_TEST=ON; see CMakeLists.txt).
