#!/usr/bin/env python3
"""First step for a new game: does its disc carry code beyond the boot EXE?

Android can't compile code at run time, so every piece of game code has to be
compiled into the APK ahead of time. A game whose whole code is the boot EXE
(Carnage Heart) is easy. Extra code on the disc means work first:

  * other PS-X EXE files (a second program the game starts, e.g. a versus
    mode or a movie player): each needs recompiling too;
  * overlays (code loaded into RAM partway through play, often in .BIN /
    .OVL / .DAT / .PAC files): play every part of the game on the PC build
    first so PSXRecomp records them, then build them into the APK. Anything
    missed runs in the slow interpreter on the device.

This reads the disc's ISO9660 file list (no extraction) and reports:
  - SYSTEM.CNF's boot file,
  - every PS-X EXE on the disc (by the "PS-X EXE" header, not the name),
  - files whose names or contents look like overlays (a hint, not proof:
    many games load code from packed archives that only a playthrough shows).

Usage: check_disc.py <disc.cue | track1.bin | disc.iso>
"""
import re
import struct
import sys
from pathlib import Path

RAW = 2352


class Disc:
    def __init__(self, path: Path):
        self.f = open(path, "rb")
        size = path.stat().st_size
        self.raw = size % RAW == 0 and size % 2048 != 0 or self._looks_raw()
        self.offset = 0
        if self.raw:
            # Mode 1 data starts 16 bytes in, Mode 2 (PS1) 24 bytes in.
            self.f.seek(16 * RAW + 15)
            mode = self.f.read(1)[0]
            self.offset = 24 if mode == 2 else 16

    def _looks_raw(self):
        self.f.seek(0)
        return self.f.read(12) == b"\x00" + b"\xff" * 10 + b"\x00"

    def sector(self, lba, count=1):
        out = bytearray()
        for i in range(count):
            if self.raw:
                self.f.seek((lba + i) * RAW + self.offset)
            else:
                self.f.seek((lba + i) * 2048)
            out += self.f.read(2048)
        return bytes(out)

    def read(self, lba, size):
        return self.sector(lba, (size + 2047) // 2048)[:size]


def walk(disc, lba, size, prefix, out, depth=0):
    if depth > 16:
        return
    data = disc.read(lba, size)
    pos = 0
    while pos < len(data):
        ln = data[pos]
        if ln == 0:
            pos = (pos // 2048 + 1) * 2048   # records don't cross sectors
            continue
        rec = data[pos:pos + ln]
        pos += ln
        if len(rec) < 34:
            break
        ext = struct.unpack_from("<I", rec, 2)[0]
        dlen = struct.unpack_from("<I", rec, 10)[0]
        flags = rec[25]
        nlen = rec[32]
        name = rec[33:33 + nlen]
        if name in (b"\x00", b"\x01"):
            continue
        name = name.decode("ascii", "replace").split(";")[0]
        path = prefix + "/" + name
        if flags & 2:
            walk(disc, ext, dlen, path, out, depth + 1)
        else:
            out.append((path, ext, dlen))


def resolve(arg: Path) -> Path:
    if arg.suffix.lower() == ".cue":
        for line in arg.read_text(errors="replace").splitlines():
            m = re.match(r'\s*FILE\s+"([^"]+)"', line, re.I)
            if m:
                return arg.parent / Path(m.group(1)).name
        raise SystemExit(f"{arg} lists no FILE")
    return arg


OVERLAY_NAMES = re.compile(r"\.(OVL|OVR|BIN|DLL|MOD|PRG|OBJ|RTN|EXE)$", re.I)


def main():
    if len(sys.argv) != 2:
        raise SystemExit(__doc__)
    path = resolve(Path(sys.argv[1]))
    disc = Disc(path)
    pvd = disc.sector(16)
    if pvd[1:6] != b"CD001":
        raise SystemExit(f"{path.name}: no ISO9660 volume found (is this track 1?)")
    root = pvd[156:156 + 34]
    files = []
    walk(disc, struct.unpack_from("<I", root, 2)[0], struct.unpack_from("<I", root, 10)[0], "", files)

    boot = None
    for p, lba, size in files:
        if p.upper() == "/SYSTEM.CNF":
            text = disc.read(lba, size).decode("ascii", "replace")
            m = re.search(r"BOOT\s*=\s*cdrom:\\?([^;\s]+)", text, re.I)
            if m:
                boot = "/" + m.group(1).replace("\\", "/").lstrip("/")
    exes, hints = [], []
    for p, lba, size in files:
        head = disc.read(lba, min(size, 2048)) if size else b""
        if head.startswith(b"PS-X EXE"):
            exes.append((p, size))
        elif OVERLAY_NAMES.search(p) and size < 4 * 1024 * 1024:
            hints.append((p, size))

    print(f"{path.name}: {len(files)} files")
    print(f"boot file: {boot or '(SYSTEM.CNF not found)'}")
    print(f"\nPS-X EXE programs ({len(exes)}):")
    for p, size in exes:
        tag = "  <- boot" if boot and p.upper() == boot.upper() else ""
        print(f"  {p}  {size // 1024} KB{tag}")
    print(f"\nPossible code files by name ({len(hints)}):")
    for p, size in hints[:60]:
        print(f"  {p}  {size // 1024} KB")
    if len(hints) > 60:
        print(f"  ... and {len(hints) - 60} more")

    print()
    if len(exes) <= 1 and not hints:
        print("Looks like one program and no obvious overlays: the easy case "
              "(like Carnage Heart). Still confirm with a PC playthrough log.")
    else:
        print("Extra code is likely. Before an Android build: recompile every "
              "PS-X EXE above, and play every part of the game on the PC build "
              "so PSXRecomp records the overlays it loads.")


if __name__ == "__main__":
    main()
