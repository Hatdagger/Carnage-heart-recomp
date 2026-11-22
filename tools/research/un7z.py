# Minimal 7z extractor for non-solid LZMA2 archives with a plain header (offline helper).
import sys, struct, lzma, os
src, out = sys.argv[1], sys.argv[2]
f = open(src, 'rb'); sh = f.read(32)
noff, nsize = struct.unpack('<QQ', sh[12:28])
f.seek(32 + noff); h = f.read(nsize); p = 0
def b():
    global p; v = h[p]; p += 1; return v
def num():
    first = b(); mask = 0x80; val = 0
    for i in range(8):
        if first & mask == 0:
            hi = first & (mask - 1); return val | (hi << (8 * i))
        val |= b() << (8 * i); mask >>= 1
    return val
assert b() == 1 and b() == 4  # Header, MainStreamsInfo
assert b() == 6  # PackInfo
packpos = num(); npack = num(); assert b() == 9
packsizes = [num() for _ in range(npack)]
while b() != 0: pass
assert b() == 7 and b() == 0x0B
nfold = num(); assert b() == 0
props = []
for _ in range(nfold):
    assert num() == 1
    flag = b(); cid = h[p:p + (flag & 0xF)]; p += flag & 0xF
    pr = b''
    if flag & 0x20:
        ln = num(); pr = h[p:p + ln]; p += ln
    props.append((cid, pr))
assert b() == 0x0C
usizes = [num() for _ in range(nfold)]
rest = h[p:]
names = rest[rest.index(b'\x00\x00\x00') - 0:] if False else None
# names: find UTF-16 block
raw = h.decode('latin1')
import re
txt = h.split(b'\x11', 1)
idx = h.index(b'C\x00a\x00r')
blob = h[idx:]; parts = blob.split(b'\x00\x00\x00'); names = []
cur = b''
s = blob.decode('utf-16-le', errors='ignore').split('\x00')
names = [x for x in s if x.endswith(('.bin', '.cue', ')'))]
print(names, packsizes, usizes, props)
files = [n for n in names if n.endswith(('.bin', '.cue'))]
pos = 32 + packpos
for i in range(nfold):
    cid, pr = props[i]; assert cid == b'\x21'
    d = pr[0]; dict_size = (2 | (d & 1)) << (d // 2 + 11)
    f.seek(pos); data = f.read(packsizes[i]); pos += packsizes[i]
    dec = lzma.LZMADecompressor(lzma.FORMAT_RAW, filters=[{'id': lzma.FILTER_LZMA2, 'dict_size': dict_size}])
    u = dec.decompress(data); assert len(u) == usizes[i], (len(u), usizes[i])
    name = os.path.basename(files[i]); open(os.path.join(out, name), 'wb').write(u); print('wrote', name, len(u))
