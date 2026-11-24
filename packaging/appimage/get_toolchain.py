"""Carnage Heart AppImage: download and unpack the portable compiler pack.

SteamOS has no CMake, Ninja or C compiler, and this framework version only
downloads its portable pack (cmake-clang-v1: clang with its own sysroot,
cmake, ninja, and a prebuilt SDL3) on Windows. AppRun calls this once when
the system has no build tools; afterwards it puts <dir>/bin on PATH and
RETCOMM_TOOLCHAIN_DIR=<dir>, so the setup wizard and the CLI use the pack.

Usage: python3 get_toolchain.py <dir> [url]
Prints a percentage per line (for zenity --progress) and "# text" lines.
Exit code 0 when <dir>/bin/cmake exists afterwards.
Standard library only.
"""
import os
import shutil
import sys
import urllib.request
import zipfile

URL = ("https://github.com/RetroPortingToolKit/RetroPorting-Toolchains/"
       "releases/latest/download/cmake-clang-v1-linux-x64.zip")


def say(pct, text=None):
    if text:
        print("# " + text, flush=True)
    print(int(pct), flush=True)


def download(url, dest):
    req = urllib.request.Request(url, headers={"User-Agent": "CarnageHeart-AppImage"})
    with urllib.request.urlopen(req, timeout=60) as r, open(dest, "wb") as f:
        total = int(r.headers.get("Content-Length") or 0)
        done, last = 0, -1
        while True:
            chunk = r.read(1 << 20)
            if not chunk:
                break
            f.write(chunk)
            done += len(chunk)
            if total:
                pct = done * 80 // total          # 0..80 for the download
                if pct != last:
                    last = pct
                    say(pct, "Downloading the compiler pack: %d of %d MB"
                        % (done >> 20, total >> 20))


def unpack(zpath, dest):
    with zipfile.ZipFile(zpath) as z:
        items = z.infolist()
        for i, info in enumerate(items):
            path = z.extract(info, dest)
            mode = (info.external_attr >> 16) & 0o7777
            if (info.external_attr >> 16) & 0o170000 == 0o120000:   # symlink
                target = open(path).read()
                os.remove(path)
                os.symlink(target, path)
            elif mode and not info.is_dir():
                os.chmod(path, mode)
            if i % 200 == 0:
                say(80 + i * 19 // max(1, len(items)), "Unpacking the compiler pack")


def main(argv):
    if len(argv) < 2:
        sys.stderr.write("usage: get_toolchain.py <dir> [url]\n")
        return 2
    dest = os.path.abspath(argv[1])
    url = argv[2] if len(argv) > 2 else URL
    tmp = dest + ".part"
    zpath = dest + ".zip.part"
    shutil.rmtree(tmp, ignore_errors=True)
    os.makedirs(os.path.dirname(dest), exist_ok=True)
    try:
        say(0, "Downloading the compiler pack (about 830 MB, only once)")
        download(url, zpath)
        unpack(zpath, tmp)
        # The zip may hold one top-level folder.
        root = tmp
        if not os.path.isfile(os.path.join(root, "bin", "cmake")):
            subs = [os.path.join(tmp, d) for d in os.listdir(tmp)]
            subs = [d for d in subs if os.path.isfile(os.path.join(d, "bin", "cmake"))]
            if subs:
                root = subs[0]
        if not os.path.isfile(os.path.join(root, "bin", "cmake")):
            raise RuntimeError("the compiler pack has no bin/cmake")
        shutil.rmtree(dest, ignore_errors=True)
        os.replace(root, dest)
        say(100, "Compiler pack ready")
        return 0
    except Exception as exc:  # noqa: BLE001 - shown to the player
        sys.stderr.write("Carnage Heart: compiler pack download failed: %s\n" % exc)
        print("# Download failed: %s" % exc, flush=True)
        return 1
    finally:
        shutil.rmtree(tmp, ignore_errors=True)
        try:
            os.remove(zpath)
        except OSError:
            pass


if __name__ == "__main__":
    sys.exit(main(sys.argv))
