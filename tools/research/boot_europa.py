"""Boot a copy to Europa (Practice) turn 1. Usage: python3 boot_europa.py PORT
It ends inside the Tactics menu: press Circle once to get to the Command menu
with Tactics selected (where turns_demo.py and hotseat.py start). Check that it
prints turn 1 and funds 500000 (the boot timing can drift)."""
import os, sys, struct
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import ch
ch.PORT = int(sys.argv[1])
w = lambda a: struct.unpack('<I', ch.read(a, 4))[0]
ch.press('start', hold=8, after=20); ch.wait(150)
for i in range(3): ch.press('x', hold=8, after=120)
ch.press('x', hold=8, after=200)
ch.press('circle', hold=8, after=60); ch.press('start', hold=8, after=60)
print('port', ch.PORT, 'turn', w(0x800df37c), 'funds', w(0x800DF38C),
      ch.shot(os.path.join(ch.OUT, 'boot_%d.png' % ch.PORT)))
