#!/usr/bin/env python3
"""Build the Android app (CarnageHeart.apk) on the player's PC.

Run by "Build Android App.bat" in the setup kit, after the PC game is
installed (the install generated the game C from the player's disc; the
Android build compiles that same C for arm64).

The first run downloads the Android tools once (pinned in android_tools.json,
checked by SHA-256): the NDK (compiler), SDK build-tools and platform, and a
JDK. They go to a per-user folder (Windows: %LOCALAPPDATA%\\CarnageHeart\\
android-tools) so kit updates don't download them again. The signing key is
kept beside them, so every later APK installs over the earlier app and keeps
its memory cards.

The APK holds code made from the player's own disc: for their devices only.
"""
import argparse
import hashlib
import json
import os
import shutil
import stat
import subprocess
import sys
import tarfile
import time
import urllib.request
import zipfile
from pathlib import Path

HERE = Path(__file__).resolve().parent
ROOT = HERE.parents[1]
PINS = HERE / "android_tools.json"
IS_WIN = os.name == "nt"
HOST = "windows" if IS_WIN else "linux"
EXE = ".exe" if IS_WIN else ""


def say(msg=""):
    print(msg, flush=True)


def user_data_dir() -> Path:
    env = os.environ.get("CH_ANDROID_HOME")
    if env:
        return Path(env)
    if IS_WIN:
        base = os.environ.get("LOCALAPPDATA") or str(Path.home() / "AppData" / "Local")
        return Path(base) / "CarnageHeart"
    return Path(os.environ.get("XDG_DATA_HOME") or Path.home() / ".local" / "share") / "carnageheart"


def long_path(p: Path) -> str:
    """Windows paths longer than 260 characters need the \\\\?\\ prefix (the
    NDK has some deep ones)."""
    s = str(p.resolve())
    if IS_WIN and not s.startswith("\\\\?\\"):
        s = "\\\\?\\" + s
    return s


def file_sha256(p: Path) -> str:
    h = hashlib.sha256()
    with open(p, "rb") as f:
        for b in iter(lambda: f.read(1 << 20), b""):
            h.update(b)
    return h.hexdigest()


def download(url: str, dest: Path, sha256: str, size: int) -> None:
    tmp = dest.with_suffix(dest.suffix + ".part")
    for attempt in range(1, 4):
        try:
            h = hashlib.sha256()
            got = 0
            last = -1
            req = urllib.request.Request(url, headers={"User-Agent": "CarnageHeart-setup"})
            with urllib.request.urlopen(req, timeout=60) as r, open(tmp, "wb") as f:
                while True:
                    b = r.read(1 << 20)
                    if not b:
                        break
                    f.write(b)
                    h.update(b)
                    got += len(b)
                    if size:
                        pct = got * 100 // size
                        if pct // 5 != last // 5:
                            last = pct
                            say(f"    {pct:3d}%  {got // (1 << 20)} / {size // (1 << 20)} MB")
            if h.hexdigest() != sha256:
                raise IOError(f"checksum mismatch for {dest.name}")
            tmp.replace(dest)
            return
        except Exception as e:  # noqa: BLE001 - retried, then reported
            say(f"    download problem ({e}); try {attempt} of 3")
            time.sleep(3)
    raise SystemExit(f"Could not download {url}. Check the internet connection and run again.")


def extract(archive: Path, dest: Path) -> None:
    if dest.exists():
        shutil.rmtree(long_path(dest))
    dest.mkdir(parents=True)
    if archive.name.endswith(".zip"):
        with zipfile.ZipFile(archive) as z:
            for info in z.infolist():
                target = dest / info.filename
                if info.is_dir():
                    os.makedirs(long_path(target), exist_ok=True)
                    continue
                os.makedirs(long_path(target.parent), exist_ok=True)
                with z.open(info) as src, open(long_path(target), "wb") as out:
                    shutil.copyfileobj(src, out, 1 << 20)
                mode = (info.external_attr >> 16) & 0o777
                if not IS_WIN and mode:
                    os.chmod(target, mode | stat.S_IRUSR)
    else:
        with tarfile.open(archive) as t:
            t.extractall(dest)


def ensure_tools(tools: Path) -> dict:
    pins = json.loads(PINS.read_text())
    tools.mkdir(parents=True, exist_ok=True)
    found = {}
    for comp in ("ndk", "build-tools", "platform", "jdk"):
        pin = pins.get(f"{comp}-{HOST}") or pins.get(f"{comp}-any")
        if not pin:
            raise SystemExit(f"no {comp} download listed for {HOST} in {PINS.name}")
        dest = tools / comp
        stamp = dest / ".carnageheart-sha256"
        if not (stamp.is_file() and stamp.read_text().strip() == pin["sha256"]):
            name = pin["url"].rsplit("/", 1)[-1]
            say(f"  Downloading {comp} ({pin['size'] // (1 << 20)} MB, once): {name}")
            arc = tools / name
            if not arc.is_file() or file_sha256(arc) != pin["sha256"]:
                download(pin["url"], arc, pin["sha256"], pin["size"])
            say(f"  Unpacking {comp}...")
            extract(arc, dest)
            stamp.write_text(pin["sha256"])
            arc.unlink()
        found[comp] = dest
    return locate(found)


def first(paths, what):
    for p in paths:
        return p
    raise SystemExit(f"could not find {what} in the downloaded Android tools")


def locate(d: dict) -> dict:
    ndk = first((p.parents[2] for p in d["ndk"].glob("*/build/cmake/android.toolchain.cmake")), "the NDK")
    bt = first((p.parent for p in d["build-tools"].rglob("aapt2" + EXE)), "aapt2")
    jar = first(d["platform"].rglob("android.jar"), "android.jar")
    jdk = first((p.parents[1] for p in d["jdk"].rglob("bin/javac" + EXE)), "the JDK")
    if not IS_WIN:
        for p in list(bt.glob("*")) + list(jdk.glob("bin/*")) + list(
                (ndk / "toolchains" / "llvm" / "prebuilt").glob("*/bin/*")):
            if p.is_file():
                p.chmod(p.stat().st_mode | 0o111)
    return {"ndk": ndk, "build_tools": bt, "platform_jar": jar, "jdk": jdk}


def cmake_and_ninja():
    """The setup kit's compiler pack (shared per-user cache) first, then PATH."""
    sys.path.insert(0, str(ROOT / "psxrecomp" / "tools"))
    try:
        from toolchain_pack import activate_toolchain_bin, resolve_toolchain_bin
        bin_dir = resolve_toolchain_bin(ROOT)
        if bin_dir:
            activate_toolchain_bin(bin_dir)
    except Exception as e:  # noqa: BLE001 - fall back to PATH
        say(f"  (compiler pack not used: {e})")
    cmake, ninja = shutil.which("cmake"), shutil.which("ninja")
    if not cmake or not ninja:
        raise SystemExit("cmake/ninja not found. Run CarnageHeart_Recompiled once first so it "
                         "installs the game and its compiler pack.")
    return cmake, ninja


def main():
    ap = argparse.ArgumentParser(description="Build CarnageHeart.apk for Android")
    ap.add_argument("--tools-dir", help="where the Android tools go (default: per-user folder)")
    ap.add_argument("--out", default=str(ROOT / "CarnageHeart.apk"))
    ap.add_argument("--tools-only", action="store_true", help="download/check the tools and stop")
    ap.add_argument("--allow-no-game", action="store_true", help=argparse.SUPPRESS)
    ap.add_argument("--jobs", type=int, default=os.cpu_count() or 2)
    a = ap.parse_args()

    say("=" * 64)
    say(" Carnage Heart: build the Android app")
    say("=" * 64)
    if not a.allow_no_game and not a.tools_only and \
            not (ROOT / "generated" / "SCUS_946.04_dispatch.c").is_file():
        raise SystemExit("\nThe game isn't installed on this PC yet. Run CarnageHeart_Recompiled "
                         "first (it turns your disc into the game), then run this again.")

    home = user_data_dir()
    tools_dir = Path(a.tools_dir) if a.tools_dir else home / "android-tools"
    say(f"\n[1/3] Android tools ({tools_dir})")
    t = ensure_tools(tools_dir)
    say("  Ready.")
    if a.tools_only:
        for k, v in t.items():
            say(f"  {k}: {v}")
        return

    say("\n[2/3] Compiler")
    cmake, ninja = cmake_and_ninja()
    say(f"  cmake: {cmake}\n  ninja: {ninja}")

    say("\n[3/3] Building the app (the first build takes about as long as the PC install)")
    cmd = [sys.executable, str(HERE / "build_apk.py"),
           "--ndk", str(t["ndk"]), "--platform-jar", str(t["platform_jar"]),
           "--build-tools", str(t["build_tools"]), "--jdk", str(t["jdk"]),
           "--cmake", cmake, "--ninja", ninja, "--jobs", str(a.jobs),
           "--keystore", str(home / "android-signing" / "carnageheart.keystore"),
           "--out", a.out]
    if a.allow_no_game:
        cmd.append("--allow-no-game")
    rc = subprocess.call(cmd)
    if rc != 0:
        raise SystemExit(f"\nThe Android build failed (exit {rc}). The lines above say where.")

    say("\n" + "=" * 64)
    say(f" Done: {a.out}")
    say("=" * 64)
    say("""
To put it on your tablet or phone:
 1. Copy CarnageHeart.apk to the device (USB cable, Google Drive, etc.),
    together with your disc files (the .cue and the three .bin files).
 2. On the device, tap CarnageHeart.apk to install it. Allow "install
    unknown apps" if asked; if Play Protect warns, tap More details, then
    Install anyway.
 3. Open Carnage Heart, tap CHOOSE DISC FILES and pick the .cue and the
    three .bin files together.

This app contains code made from your own disc: keep it for your own devices.
Building again later (after an update) makes an APK that installs over this
one and keeps your memory cards.""")


if __name__ == "__main__":
    main()
