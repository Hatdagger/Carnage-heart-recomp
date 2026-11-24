#!/usr/bin/env python3
"""Build the Carnage Heart Android app (arm64 APK) from this project.

Needs the recompiled game C in generated/ (made from the player's own disc by
`psxrecomp_cli.py generate`, which the setup kit has already done for the PC
build). The APK therefore contains code made from the player's disc: it is for
their own devices only.

Tools (the Windows setup kit downloads them once; see android_tools.py):
  --ndk DIR          Android NDK (r27 or newer)
  --platform-jar F   android.jar (SDK platform 35)
  --build-tools DIR  SDK build-tools 35 (aapt2, zipalign, lib/d8.jar, lib/apksigner.jar)
  --jdk DIR          a JDK 17+ (javac, java, keytool); default: JAVA_HOME / PATH
  --cmake / --ninja  default: on PATH

Steps: SDL3 (static) -> game library (libmain.so) -> Java (SDL glue + our
activities) -> resources -> APK -> zipalign -> sign. The signing key is made
once and kept (see --keystore) so later APKs install over earlier ones and
keep the memory cards.
"""
import argparse
import os
import shutil
import subprocess
import sys
import tarfile
import zipfile
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
ABI = "arm64-v8a"
MIN_SDK = 29        # Android 10 (getrandom / timespec_get used by the runtime)
TARGET_SDK = 35
PACKAGE = "com.hatdagger.carnageheart"
KEY_ALIAS = "carnageheart"
KEY_PASS = "carnageheart"   # personal side-load key, not a secret
EXE = ".exe" if os.name == "nt" else ""
BAT = ".bat" if os.name == "nt" else ""

# Files the runtime reads from its game folder, staged by the CMake build
# next to the library. Copied into assets/game/ and unpacked on the device.
GAME_FILES = ["game.toml", "game_options.toml", "bios", "mods", "assets"]


def log(msg):
    print(f"[android] {msg}", flush=True)


def run(cmd, **kw):
    log("$ " + " ".join(str(c) for c in cmd))
    subprocess.run([str(c) for c in cmd], check=True, **kw)


def which(name, hint=None):
    if hint:
        p = Path(hint)
        if p.is_file():
            return p
        raise SystemExit(f"not found: {hint}")
    found = shutil.which(name)
    if not found:
        raise SystemExit(f"{name} not found on PATH (pass --{name})")
    return Path(found)


def version_info():
    raw = (ROOT / "VERSION").read_text().strip().lstrip("v")
    parts = (raw.split("-")[0].split(".") + ["0", "0", "0"])[:3]
    try:
        major, minor, patch = (int(p) for p in parts)
    except ValueError:
        major, minor, patch = 0, 0, 1
    return raw, major * 10000 + minor * 100 + patch


def toolchain_args(a):
    if a.toolchain_file:
        return [f"-DCMAKE_TOOLCHAIN_FILE={a.toolchain_file}"]
    tc = Path(a.ndk) / "build" / "cmake" / "android.toolchain.cmake"
    if not tc.is_file():
        raise SystemExit(f"NDK toolchain file missing: {tc}")
    return [f"-DCMAKE_TOOLCHAIN_FILE={tc}", f"-DANDROID_ABI={ABI}",
            f"-DANDROID_PLATFORM=android-{MIN_SDK}", "-DANDROID_STL=c++_static"]


def sdl3_source(work):
    tars = sorted((ROOT / "psxrecomp" / "third_party").glob("SDL3-*.tar.gz"))
    if not tars:
        raise SystemExit("psxrecomp/third_party/SDL3-*.tar.gz is missing "
                         "(the setup kit ships it; run psxrecomp/tools/ci/vendor_deps.sh)")
    tar = tars[-1]
    dest = work / tar.name[: -len(".tar.gz")]
    if not (dest / "CMakeLists.txt").is_file():
        log(f"unpacking {tar.name}")
        with tarfile.open(tar) as t:
            t.extractall(work)
    return dest


def build_sdl3(a, work, cmake, ninja):
    src = sdl3_source(work)
    prefix = work / "sdl3-install"
    if (prefix / "lib" / "cmake" / "SDL3" / "SDL3Config.cmake").is_file():
        return src, prefix
    bld = work / "sdl3-build"
    run([cmake, "-S", src, "-B", bld, "-G", "Ninja", f"-DCMAKE_MAKE_PROGRAM={ninja}",
         *toolchain_args(a), "-DCMAKE_BUILD_TYPE=Release",
         "-DSDL_SHARED=OFF", "-DSDL_STATIC=ON", "-DSDL_TEST_LIBRARY=OFF",
         "-DSDL_TESTS=OFF", "-DSDL_EXAMPLES=OFF", f"-DCMAKE_INSTALL_PREFIX={prefix}"])
    run([cmake, "--build", bld, "--parallel", str(a.jobs)])
    run([cmake, "--install", bld])
    return src, prefix


def build_game(a, sdl_prefix, cmake, ninja):
    if not a.allow_no_game and not (ROOT / "generated" / "SCUS_946.04_dispatch.c").is_file():
        raise SystemExit("generated/ has no game C. Generate it from your disc first "
                         "(the setup kit does this when it installs the PC game).")
    bld = Path(a.build_dir)
    run([cmake, "-S", ROOT, "-B", bld, "-G", "Ninja", f"-DCMAKE_MAKE_PROGRAM={ninja}",
         *toolchain_args(a), "-DCMAKE_BUILD_TYPE=Release",
         f"-DSDL3_DIR={sdl_prefix / 'lib' / 'cmake' / 'SDL3'}"])
    run([cmake, "--build", bld, "--parallel", str(a.jobs)])
    libs = [p for p in bld.glob("lib*.so") if p.name != "libmain.so"] + list(bld.glob("libmain.so"))
    if not libs:
        raise SystemExit(f"no game library was built in {bld}")
    return bld, libs[0]


def find_llvm_strip(a):
    if a.ndk:
        for p in (Path(a.ndk) / "toolchains" / "llvm" / "prebuilt").glob("*/bin/llvm-strip" + EXE):
            return p
    s = shutil.which("llvm-strip")
    return Path(s) if s else None


def stage(a, bld, lib, stage_dir):
    if stage_dir.exists():
        shutil.rmtree(stage_dir)
    libdir = stage_dir / "lib" / ABI
    libdir.mkdir(parents=True)
    out = libdir / "libmain.so"
    strip = find_llvm_strip(a)
    if strip:
        run([strip, "--strip-unneeded", "-o", out, lib])
    else:
        shutil.copy2(lib, out)
    game = stage_dir / "assets" / "game"
    game.mkdir(parents=True)
    listed = []
    for name in GAME_FILES:
        p = bld / name
        if p.is_file():
            shutil.copy2(p, game / name)
            listed.append(name)
        elif p.is_dir():
            for f in sorted(p.rglob("*")):
                if f.is_file():
                    rel = f.relative_to(bld).as_posix()
                    (game / rel).parent.mkdir(parents=True, exist_ok=True)
                    shutil.copy2(f, game / rel)
                    listed.append(rel)
    if "game.toml" not in listed:
        raise SystemExit(f"game.toml was not staged in {bld}")
    (game / "files.txt").write_text("\n".join(listed) + "\n", encoding="utf-8")
    log(f"staged libmain.so ({out.stat().st_size // 1024} KB) and {len(listed)} game files")


def jdk_tool(a, name):
    if a.jdk:
        return Path(a.jdk) / "bin" / (name + EXE)
    if os.environ.get("JAVA_HOME"):
        p = Path(os.environ["JAVA_HOME"]) / "bin" / (name + EXE)
        if p.is_file():
            return p
    return which(name)


def build_java(a, sdl_src, work):
    jsrc = work / "java-src"
    if jsrc.exists():
        shutil.rmtree(jsrc)
    shutil.copytree(sdl_src / "android-project" / "app" / "src" / "main" / "java", jsrc)
    shutil.copytree(ROOT / "android" / "java", jsrc, dirs_exist_ok=True)
    classes = work / "classes"
    if classes.exists():
        shutil.rmtree(classes)
    classes.mkdir()
    sources = [str(p) for p in jsrc.rglob("*.java")]
    argfile = work / "javac-sources.txt"
    argfile.write_text("\n".join(f'"{s}"'.replace("\\", "\\\\") for s in sources))
    run([jdk_tool(a, "javac"), "-nowarn", "-Xlint:-options", "-encoding", "UTF-8",
         "--release", "8", "-classpath", a.platform_jar,
         "-d", classes, f"@{argfile}"])
    dex = work / "dex"
    if dex.exists():
        shutil.rmtree(dex)
    dex.mkdir()
    class_files = [str(p) for p in classes.rglob("*.class")]
    d8_args = work / "d8-inputs.txt"
    d8_args.write_text("\n".join(class_files))
    run([jdk_tool(a, "java"), "-cp", Path(a.build_tools) / "lib" / "d8.jar",
         "com.android.tools.r8.D8", "--release", "--min-api", str(MIN_SDK),
         "--lib", a.platform_jar, "--output", dex, f"@{d8_args}"])
    return dex / "classes.dex"


def build_apk(a, stage_dir, dex, work):
    name, code = version_info()
    manifest = (ROOT / "android" / "AndroidManifest.xml").read_text(encoding="utf-8")
    manifest = manifest.replace("@VERSION_CODE@", str(code)).replace("@VERSION_NAME@", name)
    mpath = work / "AndroidManifest.xml"
    mpath.write_text(manifest, encoding="utf-8")
    aapt2 = Path(a.build_tools) / ("aapt2" + EXE)
    res_zip = work / "res.zip"
    run([aapt2, "compile", "--dir", ROOT / "android" / "res", "-o", res_zip])
    base = work / "base.apk"
    run([aapt2, "link", "-o", base, "-I", a.platform_jar, "--manifest", mpath,
         "--min-sdk-version", str(MIN_SDK), "--target-sdk-version", str(TARGET_SDK),
         "--version-code", str(code), "--version-name", name, res_zip])

    unsigned = work / "unaligned.apk"
    shutil.copy2(base, unsigned)
    with zipfile.ZipFile(unsigned, "a", zipfile.ZIP_DEFLATED) as z:
        z.write(dex, "classes.dex")
        for f in sorted((stage_dir / "lib").rglob("*.so")):
            # Uncompressed so Android maps it straight from the APK
            # (extractNativeLibs=false); zipalign -P 16 page-aligns it.
            z.write(f, f.relative_to(stage_dir).as_posix(), compress_type=zipfile.ZIP_STORED)
        for f in sorted((stage_dir / "assets").rglob("*")):
            if f.is_file():
                z.write(f, f.relative_to(stage_dir).as_posix())

    aligned = work / "aligned.apk"
    run([Path(a.build_tools) / ("zipalign" + EXE), "-f", "-P", "16", "4", unsigned, aligned])

    ks = Path(a.keystore)
    if not ks.is_file():
        ks.parent.mkdir(parents=True, exist_ok=True)
        log(f"making a signing key (kept for later builds): {ks}")
        run([jdk_tool(a, "keytool"), "-genkeypair", "-keystore", ks, "-storetype", "PKCS12",
             "-storepass", KEY_PASS, "-keypass", KEY_PASS, "-alias", KEY_ALIAS,
             "-keyalg", "RSA", "-keysize", "2048", "-validity", "10000",
             "-dname", "CN=Carnage Heart Recompiled (personal build)"])
    out = Path(a.out)
    out.parent.mkdir(parents=True, exist_ok=True)
    run([jdk_tool(a, "java"), "-jar", Path(a.build_tools) / "lib" / "apksigner.jar", "sign",
         "--ks", ks, "--ks-pass", f"pass:{KEY_PASS}", "--key-pass", f"pass:{KEY_PASS}",
         "--ks-key-alias", KEY_ALIAS, "--v4-signing-enabled", "false",
         "--out", out, aligned])
    run([jdk_tool(a, "java"), "-jar", Path(a.build_tools) / "lib" / "apksigner.jar",
         "verify", out])
    log(f"done: {out} ({out.stat().st_size // (1024 * 1024)} MB, version {name} / {code})")


def main():
    ap = argparse.ArgumentParser(description=__doc__,
                                 formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--ndk")
    ap.add_argument("--toolchain-file", help="use this CMake toolchain instead of the NDK's")
    ap.add_argument("--platform-jar", required=True)
    ap.add_argument("--build-tools", required=True)
    ap.add_argument("--jdk")
    ap.add_argument("--cmake")
    ap.add_argument("--ninja")
    ap.add_argument("--build-dir", default=str(ROOT / "build-android"))
    ap.add_argument("--work-dir", default=str(ROOT / "build-android-work"))
    ap.add_argument("--keystore", default=str(ROOT / "android-signing" / "carnageheart.keystore"))
    ap.add_argument("--out", default=str(ROOT / "CarnageHeart.apk"))
    ap.add_argument("--jobs", type=int, default=os.cpu_count() or 2)
    ap.add_argument("--allow-no-game", action="store_true",
                    help="CI check: build without generated/ (the APK has no game in it)")
    ap.add_argument("--skip-native", action="store_true",
                    help="reuse the already built game library (Java/APK changes only)")
    a = ap.parse_args()
    if not a.ndk and not a.toolchain_file:
        ap.error("pass --ndk (or --toolchain-file)")

    cmake = which("cmake", a.cmake)
    ninja = which("ninja", a.ninja)
    work = Path(a.work_dir)
    work.mkdir(parents=True, exist_ok=True)

    sdl_src, sdl_prefix = build_sdl3(a, work, cmake, ninja)
    if a.skip_native:
        bld = Path(a.build_dir)
        libs = [p for p in bld.glob("lib*.so")]
        if not libs:
            raise SystemExit("--skip-native: no game library built yet")
        lib = libs[0]
    else:
        bld, lib = build_game(a, sdl_prefix, cmake, ninja)
    stage_dir = work / "stage"
    stage(a, bld, lib, stage_dir)
    dex = build_java(a, sdl_src, work)
    build_apk(a, stage_dir, dex, work)


if __name__ == "__main__":
    try:
        main()
    except subprocess.CalledProcessError as e:
        sys.exit(f"[android] step failed (exit {e.returncode}): {e.cmd[0]}")
