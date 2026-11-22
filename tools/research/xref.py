"""Find code that forms addresses inside given RAM ranges (lui + addiu/ori/load/store)."""
import os
REPO = os.environ.get('CH_REPO', os.path.abspath(os.path.join(os.path.dirname(os.path.abspath(__file__)), '..', '..')))
OUT = os.environ.get('CH_OUT', os.path.join(REPO, 'build', 'research'))
os.makedirs(OUT, exist_ok=True)
import struct, sys, bisect

EXE = os.path.join(REPO, 'disc', 'SCUS_946.04')
raw = open(EXE, 'rb').read()
LOAD = 0x80010000
code = raw[0x800:]
words = struct.unpack('<%dI' % (len(code) // 4), code[:len(code) // 4 * 4])

# function starts from the dispatch table / ranges file
funcs = []
for line in open(os.path.join(REPO, 'generated', 'SCUS_946.04_full.ranges')):
    p = line.split()
    if len(p) == 2 and p[0] == 'F':
        funcs.append(int(p[1], 16))
funcs = sorted(set(funcs))


def func_of(pc):
    i = bisect.bisect_right(funcs, pc) - 1
    return funcs[i] if i >= 0 else 0


MEMOPS = {0x20: 'lb', 0x21: 'lh', 0x23: 'lw', 0x24: 'lbu', 0x25: 'lhu',
          0x28: 'sb', 0x29: 'sh', 0x2b: 'sw', 0x09: 'addiu', 0x0d: 'ori'}


def scan(lo, hi):
    hits = []
    for i, w in enumerate(words):
        if w >> 26 != 0x0f:  # lui
            continue
        rt = (w >> 16) & 31
        hiv = (w & 0xffff) << 16
        for j in range(i + 1, min(i + 12, len(words))):
            w2 = words[j]
            op = w2 >> 26
            rs = (w2 >> 21) & 31
            if op in MEMOPS and rs == rt:
                imm = w2 & 0xffff
                off = imm if op == 0x0d else (imm - 0x10000 if imm & 0x8000 else imm)
                addr = (hiv + off) & 0xffffffff
                if lo <= addr < hi:
                    pc = LOAD + 4 * j
                    hits.append((pc, func_of(pc), MEMOPS[op], addr))
            # stop if rt is overwritten
            if op == 0x0f and ((w2 >> 16) & 31) == rt:
                break
    return hits


if __name__ == '__main__':
    lo, hi = int(sys.argv[1], 16), int(sys.argv[2], 16)
    hs = scan(lo, hi)
    byf = {}
    for pc, f, op, a in hs:
        byf.setdefault(f, []).append((pc, op, a))
    for f in sorted(byf):
        print('func %08x: ' % f + ', '.join('%s %08x' % (op, a) for pc, op, a in byf[f]))


def callers(target):
    out = []
    for i, w in enumerate(words):
        if w >> 26 == 3 and ((w & 0x3ffffff) << 2 | 0x80000000) == target:
            pc = LOAD + 4 * i
            out.append((pc, func_of(pc)))
    return out
