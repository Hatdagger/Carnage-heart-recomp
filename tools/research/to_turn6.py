"""Boot -> Europa practice -> end turns to turn 6 -> savestate slot 10."""
import os,sys,struct; sys.path.insert(0, os.path.dirname(os.path.abspath(__file__))); import ch
def turn(): return struct.unpack('<I', ch.read(0x800df37c, 4))[0]
ch.press('start',hold=8,after=20); ch.wait(150)
for i in range(3): ch.press('x',hold=8,after=120)
ch.press('x',hold=8,after=200)
ch.press('circle',hold=8,after=60); ch.press('start',hold=8,after=60)
for i in range(4): ch.press('down',hold=8,after=15)
while turn()<6:
    t0=turn(); ch.press('x',hold=6,after=20)
    for _ in range(300):
        if turn()!=t0: break
        ch.wait(20)
    ch.wait(120)
print('turn',turn(), 'saved', ch.cmd(cmd='savestate',op='save',slot='10').get('ok'))
