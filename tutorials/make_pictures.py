#!/usr/bin/env python3
"""Carnage Heart ReComp: make the tutorial pictures on this PC.

Started by the launcher's INSTALL TUTORIALS row (or by hand):

    python make_pictures.py <game folder> [--pause] [--visible] [--art-only]

The tutorials (tutorials/index.html) are shipped without any game pictures.
This script makes them from the player's own disc and own built game, and
keeps them on this PC only (tutorials/local/, never uploaded, never part of
a kit or an update):

  1. art: portraits and part pictures, read from the disc image (TIM files);
  2. screenshots: the built game runs hidden for a few minutes, a script
     (tutorials/capture_script.txt) walks through the menus of a new Practice
     game and saves pictures (src/tutorial_capture.c). Nothing is saved to
     the player's memory cards: the run uses a temporary save folder.

Then tutorials/local/manifest.js lists what was made and done.txt marks the
install as finished. Only the Python standard library is used, so the
compiler pack's Python runs it.
"""
import os
import shutil
import struct
import subprocess
import sys
import tempfile
import time
import zlib

HERE = os.path.dirname(os.path.abspath(__file__))
SHOT_TIMEOUT = 15 * 60   # seconds for the whole game run


def say(msg=""):
    print(msg, flush=True)
    try:
        with open(LOG_PATH, "a", encoding="utf-8") as f:
            f.write(time.strftime("%H:%M:%S ") + msg + "\n")
    except (OSError, NameError):
        pass


# ---------------------------------------------------------------- disc access

def read_text(path):
    with open(path, "r", encoding="utf-8", errors="replace") as f:
        return f.read()


def find_cue(kit):
    """The disc the game was installed from (disc.cfg), else a .cue in disc/."""
    for cfg in (os.path.join(kit, "disc.cfg"), os.path.join(kit, "build-release", "disc.cfg")):
        if os.path.isfile(cfg):
            line = read_text(cfg).strip().splitlines()
            if line:
                p = line[0].strip().strip('"')
                if not os.path.isabs(p):
                    p = os.path.join(os.path.dirname(cfg), p)
                if os.path.isfile(p):
                    return os.path.normpath(p)
    for folder in (kit, os.path.join(kit, "disc")):
        if os.path.isdir(folder):
            for n in sorted(os.listdir(folder)):
                if n.lower().endswith(".cue") and "carnage" in n.lower():
                    return os.path.join(folder, n)
    return None


def track1_of(cue):
    """The first data track's .bin (FILE line of TRACK 01)."""
    current = None
    for line in read_text(cue).splitlines():
        s = line.strip()
        if s.upper().startswith("FILE "):
            q = s.find('"')
            if q >= 0:
                current = s[q + 1:s.rfind('"')]
            else:
                current = s.split()[1]
        elif s.upper().startswith("TRACK ") and current:
            p = os.path.join(os.path.dirname(cue), current)
            return p if os.path.isfile(p) else None
    return None


class Disc:
    """ISO9660 inside a MODE2/2352 track (PlayStation)."""

    def __init__(self, path):
        self.f = open(path, "rb")
        self.files = {}
        pvd = self.sector(16)
        if pvd[1:6] != b"CD001":
            raise ValueError("not a PlayStation data track")
        root = pvd[156:190]
        self.walk(struct.unpack("<I", root[2:6])[0], struct.unpack("<I", root[10:14])[0], "/")

    def sector(self, n):
        self.f.seek(n * 2352 + 24)
        return self.f.read(2048)

    def read(self, lba, size):
        out = b"".join(self.sector(lba + i) for i in range((size + 2047) // 2048))
        return out[:size]

    def walk(self, lba, size, path):
        d = self.read(lba, size)
        p = 0
        while p < len(d):
            ln = d[p]
            if ln == 0:
                p = (p // 2048 + 1) * 2048
                continue
            nl = d[p + 32]
            nm = d[p + 33:p + 33 + nl]
            e_lba = struct.unpack("<I", d[p + 2:p + 6])[0]
            e_size = struct.unpack("<I", d[p + 10:p + 14])[0]
            if nm not in (b"\x00", b"\x01"):
                name = nm.decode("latin1").split(";")[0]
                if d[p + 25] & 2:
                    self.walk(e_lba, e_size, path + name + "/")
                else:
                    self.files[path + name] = (e_lba, e_size)
            p += ln

    def file(self, path):
        lba, size = self.files[path]
        return self.read(lba, size)


# ------------------------------------------------------------------ pictures

def write_png(path, w, h, rows):
    raw = b"".join(b"\x00" + r for r in rows)

    def chunk(t, d):
        return struct.pack(">I", len(d)) + t + d + struct.pack(">I", zlib.crc32(t + d) & 0xFFFFFFFF)
    with open(path, "wb") as f:
        f.write(b"\x89PNG\r\n\x1a\n" + chunk(b"IHDR", struct.pack(">IIBBBBB", w, h, 8, 6, 0, 0, 0))
                + chunk(b"IDAT", zlib.compress(raw, 9)) + chunk(b"IEND", b""))


def c16(v):
    if v == 0:
        return b"\x00\x00\x00\x00"
    r, g, b = (v & 31) << 3, ((v >> 5) & 31) << 3, ((v >> 10) & 31) << 3
    return bytes((r | r >> 5, g | g >> 5, b | b >> 5, 255))


def tim_to_png(data, out):
    magic, flags = struct.unpack("<II", data[:8])
    if magic != 0x10:
        raise ValueError("not a TIM")
    bpp, p, clut = flags & 3, 8, None
    if flags & 8:
        ln, _x, _y, cw, chh = struct.unpack("<IHHHH", data[p:p + 12])
        cl = struct.unpack("<%dH" % (cw * chh), data[p + 12:p + 12 + cw * chh * 2])
        clut = [c16(v) for v in cl[:16 if bpp == 0 else 256]]
        p += ln
    ln, _x, _y, w, h = struct.unpack("<IHHHH", data[p:p + 12])
    px = data[p + 12:p + 12 + w * h * 2]
    rows = []
    for r in range(h):
        line = px[r * w * 2:(r + 1) * w * 2]
        if bpp == 0:
            rows.append(b"".join(clut[b & 15] + clut[b >> 4] for b in line))
        elif bpp == 1:
            rows.append(b"".join(clut[b] for b in line))
        else:
            rows.append(b"".join(c16(v) for v in struct.unpack("<%dH" % w, line)))
    width = w * 4 if bpp == 0 else w * 2 if bpp == 1 else w
    write_png(out, width, h, rows)


def png_looks_real(path):
    """False for a missing, empty or single-colour picture (a hidden window
    that never drew anything)."""
    try:
        with open(path, "rb") as f:
            d = f.read()
        if d[:8] != b"\x89PNG\r\n\x1a\n":
            return False
        p, idat = 8, b""
        while p < len(d):
            ln = struct.unpack(">I", d[p:p + 4])[0]
            t = d[p + 4:p + 8]
            if t == b"IDAT":
                idat += d[p + 8:p + 8 + ln]
            p += 12 + ln
        raw = zlib.decompress(idat)
        return len(set(raw[::97])) > 24
    except Exception:
        return False


def shrink_png(path):
    """The game saves its screenshots with light compression; pack them
    tighter (same pixels) so the tutorials folder stays small."""
    try:
        with open(path, "rb") as f:
            d = f.read()
        p, idat, head = 8, b"", None
        while p < len(d):
            ln = struct.unpack(">I", d[p:p + 4])[0]
            t = d[p + 4:p + 8]
            if t == b"IHDR":
                head = d[p + 8:p + 8 + ln]
            elif t == b"IDAT":
                idat += d[p + 8:p + 8 + ln]
            p += 12 + ln
        if not head or head[10:13] != b"\x00\x00\x00":
            return
        packed = zlib.compress(zlib.decompress(idat), 9)

        def chunk(t, c):
            return struct.pack(">I", len(c)) + t + c + struct.pack(">I", zlib.crc32(t + c) & 0xFFFFFFFF)
        out = b"\x89PNG\r\n\x1a\n" + chunk(b"IHDR", head) + chunk(b"IDAT", packed) + chunk(b"IEND", b"")
        if len(out) < len(d):
            with open(path, "wb") as f:
                f.write(out)
    except Exception:
        pass


def make_art(disc, art_dir):
    os.makedirs(art_dir, exist_ok=True)
    faces, parts = [], []
    for path in sorted(disc.files):
        name = None
        if path.startswith("/GRA/FACE/") and path.endswith(".TIM"):
            name = "face-" + path[len("/GRA/FACE/MAN"):-4] + ".png"
            dest = faces
        elif path.startswith("/MODEL/SNP/") and path.endswith(".TIM"):
            group = path.split("/")[3]
            group = "LCH" if group.startswith("LCH") else group
            stem = os.path.basename(path)[:-4].rstrip("_").lower()
            name = "part-%s-%s.png" % (group.lower(), stem)
            dest = parts
        if not name:
            continue
        try:
            tim_to_png(disc.file(path), os.path.join(art_dir, name))
            dest.append(name)
        except Exception as e:  # one bad picture never stops the rest
            say("  skipped %s (%s)" % (path, e))
    return faces, parts


# ---------------------------------------------------------------- screenshots

def game_exe(kit):
    """The built game (the exe at the kit root is only the setup program)."""
    for n in ("CarnageHeart_Recompiled.exe", "CarnageHeart_Recompiled"):
        p = os.path.join(kit, "build-release", n)
        if os.path.isfile(p):
            return p
    return None


def run_capture(exe, cue, script, out_dir, hidden):
    tmp_saves = tempfile.mkdtemp(prefix="ch-tutorial-saves-")
    env = dict(os.environ)
    env.update({
        "CH_TUTORIAL_CAPTURE": script,
        "CH_TUTORIAL_OUT": out_dir,
        "SDL_AUDIO_DRIVER": "dummy",
        "CH_POINTER": "0",
        "CH_NO_AUTO_UPDATE": "1",
    })
    args = [exe, "--no-launcher", "--renderer", "opengl", "--memcard-dir", tmp_saves, "--disc", cue]
    if hidden:
        args.insert(1, "--hidden-window")
    else:
        args += ["--window-title", "Making tutorial pictures - please don't touch this window"]
    with open(os.path.join(out_dir, "game-output.txt"), "w") as log:
        proc = subprocess.Popen(args, cwd=os.path.dirname(exe), env=env, stdout=log, stderr=subprocess.STDOUT)
        start, last = time.time(), -1
        while proc.poll() is None:
            n = len([x for x in os.listdir(out_dir) if x.endswith(".png")])
            if n != last and n and (n % 5 == 0 or last < 0):
                say("  %d picture%s taken" % (n, "" if n == 1 else "s"))
                last = n
            if time.time() - start > SHOT_TIMEOUT:
                say("  The game took too long; stopping it.")
                proc.kill()
                break
            time.sleep(2)
        proc.wait()
    shutil.rmtree(tmp_saves, ignore_errors=True)
    shots = sorted(x for x in os.listdir(out_dir) if x.endswith(".png"))
    good = [x for x in shots if png_looks_real(os.path.join(out_dir, x))]
    ok = os.path.isfile(os.path.join(out_dir, "capture-ok.txt"))
    return ok, good, shots


def tail(path, n=12):
    try:
        return read_text(path).splitlines()[-n:]
    except OSError:
        return []


# --------------------------------------------------------------------- main

def js_list(names):
    return "[" + ",".join('"%s"' % n for n in names) + "]"


def main():
    global LOG_PATH
    args = [a for a in sys.argv[1:] if not a.startswith("--")]
    flags = {a for a in sys.argv[1:] if a.startswith("--")}
    kit = os.path.abspath(args[0]) if args else os.path.dirname(HERE)
    local = os.path.join(kit, "tutorials", "local")
    os.makedirs(local, exist_ok=True)
    LOG_PATH = os.path.join(local, "make-pictures-log.txt")
    with open(LOG_PATH, "w", encoding="utf-8") as f:
        f.write("Carnage Heart tutorial pictures, %s\n" % time.strftime("%Y-%m-%d %H:%M"))

    say("CARNAGE HEART - MAKING THE TUTORIAL PICTURES")
    say("The pictures come from your own disc and game and stay on this PC.")
    say()
    cue = find_cue(kit)
    bin1 = track1_of(cue) if cue else None
    if not bin1:
        say("Your Carnage Heart disc image was not found (disc.cfg).")
        say("Install the game from the launcher first, then try again.")
        return 2
    say("Disc: " + cue)

    # 1. Art from the disc.
    say("Step 1 of 2: pictures from the disc...")
    try:
        disc = Disc(bin1)
        faces, parts = make_art(disc, os.path.join(local, "art.new"))
    except Exception as e:
        say("  Could not read the disc: %s" % e)
        return 2
    say("  %d portraits, %d part pictures" % (len(faces), len(parts)))

    # 2. Screenshots from the built game.
    shots = []
    if "--art-only" not in flags:
        exe = game_exe(kit)
        script = os.path.join(HERE, "capture_script.txt")
        if not exe:
            say("The built game was not found (build-release). Install the game first.")
            return 2
        say("Step 2 of 2: screenshots. The game runs hidden for a few minutes;")
        say("you can keep using the PC. Please wait...")
        out = os.path.join(local, "shots.new")
        shutil.rmtree(out, ignore_errors=True)
        os.makedirs(out)
        hidden = "--visible" not in flags
        ok, good, taken = run_capture(exe, cue, script, out, hidden)
        if hidden and (not good or len(good) < len(taken)):
            say("  The hidden window gave no pictures on this PC. Trying again in a")
            say("  normal window: please don't click in it until it closes by itself.")
            shutil.rmtree(out, ignore_errors=True)
            os.makedirs(out)
            ok, good, taken = run_capture(exe, cue, script, out, False)
        for line in tail(os.path.join(out, "capture-log.txt"), 3):
            say("  game: " + line)
        if not good:
            say("No screenshots could be made. The lessons will use drawings instead.")
            say("Details: " + os.path.join(out, "capture-log.txt"))
        elif not ok:
            say("  Only part of the screenshots could be made (%d)." % len(good))
        shots = good
        for n in shots:
            shrink_png(os.path.join(out, n))
        old = os.path.join(local, "shots")
        if shots or not os.path.isdir(old):
            shutil.rmtree(old, ignore_errors=True)
            os.replace(out, old)
            for extra in os.listdir(old):
                if not extra.endswith(".png") and extra not in ("capture-log.txt",):
                    os.remove(os.path.join(old, extra))
        else:
            shots = sorted(x for x in os.listdir(old) if x.endswith(".png"))
            shutil.rmtree(out, ignore_errors=True)
    elif os.path.isdir(os.path.join(local, "shots")):
        shots = sorted(x for x in os.listdir(os.path.join(local, "shots")) if x.endswith(".png"))

    art = os.path.join(local, "art")
    shutil.rmtree(art, ignore_errors=True)
    os.replace(os.path.join(local, "art.new"), art)

    version = "dev"
    vf = os.path.join(kit, "VERSION")
    if os.path.isfile(vf):
        version = read_text(vf).strip() or "dev"
    with open(os.path.join(local, "manifest.js"), "w", encoding="utf-8") as f:
        f.write("/* Written by make_pictures.py on this PC. Not part of the kit. */\n")
        f.write("window.CH_LOCAL = {version:\"%s\", made:\"%s\",\n" % (version, time.strftime("%Y-%m-%d %H:%M")))
        f.write(" shots:%s,\n" % js_list([s[:-4] for s in shots]))
        f.write(" faces:%s,\n" % js_list(faces))
        f.write(" parts:%s};\n" % js_list(parts))
    with open(os.path.join(local, "done.txt"), "w", encoding="utf-8") as f:
        f.write("%s\n%s\n%d shots\n" % (version, time.strftime("%Y-%m-%d %H:%M"), len(shots)))
    say()
    say("Done: %d screenshots, %d portraits, %d part pictures." % (len(shots), len(faces), len(parts)))
    say("In the launcher, TUTORIALS now opens the lessons.")
    return 0


if __name__ == "__main__":
    LOG_PATH = os.devnull
    try:
        rc = main()
    except KeyboardInterrupt:
        rc = 1
    except Exception as e:
        say("Something went wrong: %r" % (e,))
        rc = 1
    if "--pause" in sys.argv:
        try:
            input("\nPress Enter to close this window. ")
        except EOFError:
            pass
    sys.exit(rc)
