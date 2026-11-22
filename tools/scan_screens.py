"""scan_screens.py usa.n  (regenerates the table in src/screen_labels.c)

usa.n is a one-instruction-per-line listing of SCUS_946.04: "ADDR mnemonic operands",
ADDR in lowercase hex without 0x, e.g. from llvm-objdump (mipsel, mips1) of the
EXE text loaded at 0x80010000, with symbol annotations stripped.

Prints -> C table rows: one per menu screen (function that calls
HotspotNavigate 0x8003E89C or HotspotDrawHitTest 0x8003EEC4), with the extra
buttons it reads (its own code plus helper routines that aren't screens).
Cross/Circle are left out (left click / Back). Triangle used only as the
'hold to jump between boxes' modifier is left out. Hints: QUIT (Select opens the
quit box 0x8001B0C0), NEXTBASE (L2 cycles bases via 0x800E0154), NEXTUNIT (L2
cycles units via 0x800E017C), CHIPS (R2 opens the chip list 0x8004591C).
Also prints the battle row (not a hotspot screen; see src/screen_labels.c)."""
import re, sys, bisect

def load(path):
    out = []
    for l in open(path):
        a, ins = l.rstrip("\n").split(" ", 1)
        out.append((int(a, 16), ins))
    return out

def funcs(L):  # function starts = every jal target
    return sorted({int(m.group(1), 16) for _, ins in L for m in [re.match(r"jal\s+0x([0-9a-f]+)", ins)] if m})
L = load(sys.argv[1]); F = funcs(L); idx = {a: i for i, (a, _) in enumerate(L)}
B = {0x10: "TRI", 0x80: "SQU", 0x1: "L2", 0x2: "R2", 0x4: "L1", 0x8: "R1", 0x100: "SEL", 0x800: "STA"}
SHARED = {0x8001b0c0, 0x8003e154, 0x8003e89c, 0x8003eec4, 0x8003e338, 0x8003e4c8, 0x8003d554}

def callers(t):
    out = set()
    for a, ins in L:
        if re.match(r"jal\s+0x", ins) and int(ins.split()[-1], 16) == t:
            out.add(F[bisect.bisect_right(F, a) - 1])
    return out
SCREENS = sorted(callers(0x8003e89c) | callers(0x8003eec4))

def end_of(f):
    j = bisect.bisect_right(F, f); return F[j] if j < len(F) else f + 0x4000

def scan(f):
    i = idx.get(f)
    res = {"bits": 0, "quit": False, "nextbase": False, "nextunit": False, "chips": False, "calls": set()}
    if i is None: return res
    end = end_of(f); hi = {}; br = {}
    while i < len(L) and L[i][0] < end:
        a, ins = L[i]
        m = re.match(r"lui\s+\$(\w+), (0x[0-9a-f]+)$", ins)
        if m: hi[m.group(1)] = int(m.group(2), 16) << 16
        m = re.match(r"lw\s+\$(\w+), (-?0x[0-9a-f]+)\(\$(\w+)\)", ins)
        if m and m.group(3) in hi and ((hi[m.group(3)] + int(m.group(2), 16)) & 0xffffffff) == 0x800E0134:
            br[m.group(1)] = i
        m = re.match(r"andi\s+\$(\w+), \$(\w+), (0x[0-9a-f]+)", ins)
        if m and m.group(2) in br and i - br[m.group(2)] < 12:
            v = int(m.group(3), 16)
            if v != 0xfff:
                for b in B:
                    if not v & b: continue
                    nxt = " ".join(x for _, x in L[i:i + 11])
                    if b == 0x10 and "jal\t0x8003e89c" in nxt: continue   # nav modifier
                    near = " ".join(x for _, x in L[i:i + 14])
                    prev = " ".join(x for _, x in L[i - 3:i])
                    if b == 0x1:
                        if "0x154(" in near: res["nextbase"] = True
                        elif "0x17c(" in near: res["nextunit"] = True
                        else: continue          # L2 held as a modifier / unknown
                    # Select checked right after Circle (or Start after Cross) in
                    # the same branch is just a second Back / OK button.
                    if b == 0x100 and "andi" in prev and ", 0x20" in prev: continue
                    if b == 0x800 and "andi" in prev and ", 0x40" in prev: continue
                    # R2 that opens the chip list (chip program grid).
                    if b == 0x2 and "jal\t0x8004591c" in " ".join(x for _, x in L[i:i + 30]):
                        res["chips"] = True

                    res["bits"] |= b
        m = re.match(r"jal\s+0x([0-9a-f]+)", ins)
        if m:
            c = int(m.group(1), 16); res["calls"].add(c)
            if c == 0x8001b0c0: res["quit"] = True
        i += 1
    return res

rows = []
for f in SCREENS:
    r = scan(f); bits = r["bits"]; quit = r["quit"]; nb = r["nextbase"]; nu = r["nextunit"]; ch = r["chips"]
    for c in r["calls"] - SHARED - set(SCREENS):
        s = scan(c); bits |= s["bits"]; quit |= s["quit"]; nb |= s["nextbase"]; nu |= s["nextunit"]; ch |= s["chips"]
    bits &= ~0xA  # R1/R2: only as hold-with-D-pad modifiers (grid scroll)
    if ch: bits |= 0x2
    flags = []
    if quit: flags.append("H_QUIT")
    if nb: flags.append("H_NEXTBASE")
    if nu: flags.append("H_NEXTUNIT")
    if ch: flags.append("H_CHIPS")
    rows.append((f, end_of(f), bits, "|".join(flags) or "0"))
# Battle: L2 / Select / Start (read by 0x800220F4); noted via 0x8001FB04.
rows.append((0x8001FB04, 0x8001FB08, 0x901, "H_BATTLE"))
rows.sort()
for f, e, b, fl in rows:
    print(f"    {{ 0x{f:08X}u, 0x{e:08X}u, 0x{b:03X}u, {fl} }},")
