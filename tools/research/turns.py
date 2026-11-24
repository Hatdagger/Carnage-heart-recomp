import ch, struct, sys, time
def turn(): return struct.unpack('<I', ch.read(0x800df37c, 4))[0]
def units():
    d = ch.read(0x80134b6c, 64*22); out = []
    for i in range(64):
        r = d[22*i:22*i+22]
        if r[0]:
            v = struct.unpack('<BBhhHHBBhhhHH', r)
            out.append((i,) + v)
    return out
n = int(sys.argv[1])
for k in range(n):
    t0 = turn()
    ch.press('x', hold=5, after=20)
    for _ in range(200):
        if turn() != t0: break
        ch.wait(20)
    ch.wait(60)
    ch.shot(os.path.join(ch.OUT, 'turn%d.png' % turn()))
    print('turn', turn(), [u for u in units()])
    sys.stdout.flush()
