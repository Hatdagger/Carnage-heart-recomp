"""Tiny driver for the Carnage Heart recomp debug server (port 4370)."""
import os
REPO = os.environ.get('CH_REPO', os.path.abspath(os.path.join(os.path.dirname(os.path.abspath(__file__)), '..', '..')))
OUT = os.environ.get('CH_OUT', os.path.join(REPO, 'build', 'research'))
os.makedirs(OUT, exist_ok=True)
import sys, time, json, shutil
sys.path.insert(0, os.path.join(REPO, 'psxrecomp', 'tools'))
import debug_client as dc

BTN = dict(select=0x0001, start=0x0008, up=0x0010, right=0x0020, down=0x0040,
           left=0x0080, l2=0x0100, r2=0x0200, l1=0x0400, r1=0x0800,
           tri=0x1000, circle=0x2000, x=0x4000, square=0x8000)
_s = None


def cmd(**kw):
    s = dc.connect(port=4370, timeout=60)
    try:
        return dc.send_cmd(s, kw)
    finally:
        s.close()


def frame():
    return cmd(cmd='frame')['frame']


def wait(frames):
    f0 = frame()
    while frame() < f0 + frames:
        time.sleep(0.02)


def press(*names, hold=4, after=12):
    m = 0
    for n in names:
        m |= BTN[n]
    cmd(cmd='set_input', buttons='%04x' % (0xFFFF & ~m))
    wait(hold)
    cmd(cmd='set_input', buttons='ffff')
    wait(after)


def seq(s, hold=4, after=12):
    """seq('down down x') presses each in turn."""
    for tok in s.split():
        n, _, k = tok.partition('*')
        for _ in range(int(k or 1)):
            press(*n.split('+'), hold=hold, after=after)


def shot(path=os.path.join(OUT, 'shot.png')):
    import os
    if os.path.exists(path): os.remove(path)
    cmd(cmd='screenshot', path=path)
    return path


def read(addr, n):
    r = cmd(cmd='read_ram', addr='%x' % addr, len=n)
    return bytes.fromhex(r.get('hex') or r.get('data'))


def write(addr, data):
    # write_ram only takes one byte ('val'); write_bulk takes up to 1024 bytes as hex.
    return cmd(cmd='write_bulk', addr='%x' % addr, hex=data.hex())


if __name__ == '__main__':
    a = sys.argv[1:]
    if a[0] == 'seq':
        seq(' '.join(a[1:]))
        print(shot())
    elif a[0] == 'shot':
        print(shot(a[1] if len(a) > 1 else os.path.join(OUT, 'shot.png')))
    elif a[0] == 'wait':
        wait(int(a[1])); print(shot())
    else:
        print(json.dumps(cmd(cmd=a[0], **dict(x.split('=', 1) for x in a[1:])), indent=1)[:3000])


POINTER = 0x800DF368  # menu pointer x, y (s32 each, screen pixels)


def click(x, y, after=40):
    """Put the menu pointer at (x, y) and press X. Faster than steering it with the d-pad."""
    import struct
    write(POINTER, struct.pack('<ii', x, y))
    wait(3)
    press('x', hold=8, after=after)


def dump(path=None):
    """Full 2 MB main RAM."""
    out = bytearray()
    for off in range(0, 0x200000, 0x10000):
        out += read(0x80000000 + off, 0x10000)
    if path:
        open(path, 'wb').write(out)
    return bytes(out)
