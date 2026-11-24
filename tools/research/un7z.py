# Minimal 7z extractor (offline helper): plain or compressed (encoded) header, single-coder
# LZMA or LZMA2 folders, several files per folder. Usage: un7z.py ARCHIVE OUTDIR
import sys, struct, lzma, os
src, out = sys.argv[1], sys.argv[2]
f = open(src, 'rb'); sh = f.read(32)
noff, nsize = struct.unpack('<QQ', sh[12:28])
f.seek(32 + noff); H = f.read(nsize)


class R:
    def __init__(s, d): s.d = d; s.p = 0
    def b(s): v = s.d[s.p]; s.p += 1; return v
    def num(s):
        first = s.b(); mask = 0x80; val = 0
        for i in range(8):
            if first & mask == 0:
                return val | ((first & (mask - 1)) << (8 * i))
            val |= s.b() << (8 * i); mask >>= 1
        return val
    def bits(s, n):
        out = []; m = 0; v = 0
        for i in range(n):
            if m == 0: v = s.b(); m = 0x80
            out.append(bool(v & m)); m >>= 1
        return out
    def defbits(s, n):
        a = s.b()
        return [True] * n if a else s.bits(n)


def streams_info(r):
    si = {'packpos': 0, 'packsizes': [], 'folders': [], 'usizes': [], 'nsub': None, 'subsizes': None}
    while True:
        t = r.b()
        if t == 0: return si
        if t == 6:
            si['packpos'] = r.num(); n = r.num()
            while True:
                t2 = r.b()
                if t2 == 0: break
                if t2 == 9: si['packsizes'] = [r.num() for _ in range(n)]
                elif t2 == 0x0A:
                    d = r.defbits(n); [r.d[r.p:r.p + 4] for x in d if x]; r.p += 4 * sum(d)
        elif t == 7:
            assert r.b() == 0x0B; nf = r.num(); assert r.b() == 0
            for _ in range(nf):
                nc = r.num(); coders = []
                for _ in range(nc):
                    fl = r.b(); cid = r.d[r.p:r.p + (fl & 0xF)]; r.p += fl & 0xF
                    assert not fl & 0x10
                    pr = b''
                    if fl & 0x20:
                        ln = r.num(); pr = r.d[r.p:r.p + ln]; r.p += ln
                    coders.append((cid, pr))
                assert nc == 1, coders
                si['folders'].append(coders[0])
            assert r.b() == 0x0C
            si['usizes'] = [r.num() for _ in range(nf)]
            t2 = r.b()
            if t2 == 0x0A:
                d = r.defbits(nf); r.p += 4 * sum(d); t2 = r.b()
            assert t2 == 0
        elif t == 8:
            nf = len(si['folders']); nsub = [1] * nf; sizes = []
            t2 = r.b()
            if t2 == 0x0D:
                nsub = [r.num() for _ in range(nf)]; t2 = r.b()
            for i in range(nf):
                if nsub[i] == 0: continue
                acc = 0; ss = []
                if t2 == 9:
                    for _ in range(nsub[i] - 1):
                        v = r.num(); ss.append(v); acc += v
                ss.append(si['usizes'][i] - acc); sizes.append(ss)
            if t2 == 9: t2 = r.b()
            while t2 != 0:
                if t2 == 0x0A:
                    n = sum(k for k in nsub)
                    d = r.defbits(n); r.p += 4 * sum(d)
                t2 = r.b()
            si['nsub'] = nsub; si['subsizes'] = sizes
        else:
            raise ValueError(t)


def decode(si, i, pos):
    cid, pr = si['folders'][i]
    f.seek(pos); data = f.read(si['packsizes'][i])
    if cid == b'\x21':
        d = pr[0]; ds = (2 | (d & 1)) << (d // 2 + 11)
        flt = {'id': lzma.FILTER_LZMA2, 'dict_size': ds}
    elif cid == b'\x03\x01\x01':
        p0 = pr[0]; ds = struct.unpack('<I', pr[1:5])[0]
        flt = {'id': lzma.FILTER_LZMA1, 'dict_size': ds, 'lc': p0 % 9, 'lp': (p0 // 9) % 5, 'pb': p0 // 45}
    elif cid == b'\x00':
        return data
    else:
        raise ValueError(cid.hex())
    dec = lzma.LZMADecompressor(lzma.FORMAT_RAW, filters=[flt])
    u = dec.decompress(data, si['usizes'][i])
    return u


r = R(H)
t = r.b()
if t == 0x17:
    si = streams_info(r)
    H = decode(si, 0, 32 + si['packpos']); r = R(H); t = r.b()
assert t == 1
t = r.b()
if t == 2:
    while r.b() != 0: pass
    t = r.b()
assert t == 4
si = streams_info(r)
t = r.b(); names = []; empty = []
if t == 5:
    nfiles = r.num()
    while True:
        pt = r.b()
        if pt == 0: break
        sz = r.num(); body = r.d[r.p:r.p + sz]; r.p += sz
        if pt == 0x0E:
            rr = R(body); empty = rr.bits(nfiles)
        elif pt == 0x11:
            names = body[1:].decode('utf-16-le').split('\x00')[:nfiles]
print(names, si['usizes'])
os.makedirs(out, exist_ok=True)
pos = 32 + si['packpos']; ni = 0
nonempty = [n for k, n in enumerate(names) if not (empty and empty[k])]
for i in range(len(si['folders'])):
    u = decode(si, i, pos); pos += si['packsizes'][i]
    subs = si['subsizes'][i] if si['subsizes'] else [len(u)]
    o = 0
    for s in subs:
        name = os.path.basename(nonempty[ni]); ni += 1
        open(os.path.join(out, name), 'wb').write(u[o:o + s]); o += s
        print('wrote', name, s)
