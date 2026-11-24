# Android build

Builds an arm64 APK of Carnage Heart from **your own disc**. The APK holds
code made from your disc, so it is for your own devices only.

Players don't run this by hand: the Windows setup kit's "Build Android app"
step downloads the tools once and runs it (in progress). For developers:

```sh
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
- `cmake/ch_android.cmake`: Android switches (no launcher UI, wizard,
  netplay, Vulkan, OpenXR, rewind); the game becomes `libmain.so` for
  SDLActivity. The framework submodule is not patched.
- `src/android/`: `SDL_main` wrapper (app files folder = game folder, logs to
  logcat and `files/carnageheart.log`), getcontext/swapcontext for the BIOS
  fibers (Bionic has none), two desktop-GL stubs.
- `android/`: manifest, icons, and the Java shell. `SetupActivity` unpacks the
  game files and imports the disc (.cue + .bin, or .chd) once through Android's
  file picker; `GameActivity` (SDLActivity) runs the game with the software
  renderer. SDL's own Java glue is taken from the vendored SDL3 tarball at
  build time so it always matches the C library.
- Minimum Android 10 (API 29), target API 35, 16 KB page aligned.
