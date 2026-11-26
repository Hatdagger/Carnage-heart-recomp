"""Checks the market stance counters in a folder of turn files (version 5):
battles = all battle records so far; each player's kills = the other
player's units wiped out in them; and the counters a copy set on loading
(its log) match the file."""
import sys, struct, glob, os, re
d = sys.argv[1]; logs = sys.argv[2:]
HEADER, TABLES = 52, 8 + 64*22 + 16*92 + 200*12
CARDS, BATTLE = 28*1152, 42
battles = 0; kills = {0: 0, 1: 0}; bad = 0; n_files = 0; files = {}
for path in sorted(glob.glob(os.path.join(d, 'turn-*.cht'))):
    b = open(path, 'rb').read()
    ver, role = struct.unpack_from('<H', b, 4)[0], b[6]
    seq = struct.unpack_from('<I', b, 8)[0]
    if ver < 5: continue
    p = HEADER + TABLES + CARDS
    p += 1 + b[p] * 8
    nb = b[p]; recs = [b[p+1+i*BATTLE:p+1+(i+1)*BATTLE] for i in range(nb)]
    for r in recs:
        for side in (0, 1):
            ents = [struct.unpack_from('<hhh', r, 6 + (side*3+s)*6) for s in range(3)]
            ents = [e for e in ents if e[0] >= 0]
            if ents and all(e[1] <= 0 for e in ents):
                # r[side] is the unit in the sender's numbering: < 32 = the sender's
                kills[1 - role if r[side] < 32 else role] += 1
    battles += nb
    tag, bt, recv, send = struct.unpack_from('<4sIII', b, len(b) - 16)
    want = (battles, kills[1 - role], kills[role])
    got = (bt, recv, send)
    ok = tag == b'STC1' and got == want
    bad += not ok; n_files += 1; files[seq] = got
    if not ok: print('FAIL file %d: counters %s, from the battle records %s' % (seq, got, want))
for log in logs:
    loaded = None
    for line in open(log):
        m = re.search(r'loaded (my own )?turn file (\d+)', line)
        if m: loaded = (int(m.group(2)), bool(m.group(1)))
        m = re.search(r'market stance counters: (\d+) battle\(s\), my kills (\d+), theirs (\d+)', line)
        if m and loaded and loaded[0] in files:
            bt, recv, send = files[loaded[0]]
            want = (bt, send, recv) if loaded[1] else (bt, recv, send)
            got = tuple(int(x) for x in m.groups())
            if got != want: bad += 1; print('FAIL %s: file %d set %s, file says %s' % (log, loaded[0], got, want))
print('%s stance counters in %d turn file(s): %d battle(s), kills A %d, B %d' %
      ('FAILED' if bad else 'ok  ', n_files, battles, kills[0], kills[1]))
sys.exit(1 if bad or not n_files else 0)
