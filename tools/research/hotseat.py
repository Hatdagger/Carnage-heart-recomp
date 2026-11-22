"""Hot-seat test helpers (multiplayer plan step 4). Run the game with CH_HOTSEAT=1 and its
output in a log file (CH_LOG, default build/research/game.log): the helpers wait for the
[hotseat] lines in it. Example: python3 tools/research/hotseat.py demo"""
import sys, os, struct; sys.path.insert(0, os.path.dirname(os.path.abspath(__file__))); import ch
LOG = os.environ.get('CH_LOG', os.path.join(ch.REPO, 'build', 'research', 'game.log'))
w=lambda a: struct.unpack('<I',ch.read(a,4))[0]
def turn(): return w(0x800df37c)
def end_turn():
    """From the Command menu (Tactics selected): End Turn, wait until the side swaps."""
    log0=open(LOG,'rb').read().count(b'[hotseat] side')
    for i in range(4): ch.press('down',hold=8,after=15)
    ch.press('x',hold=6,after=20)
    for _ in range(400):
        if open(LOG,'rb').read().count(b'[hotseat] side')>log0: break
        ch.wait(20)
    ch.wait(200)
def state():
    pc,ok,bs,un=[w(a) for a in (0x800E0014,0x800E014C,0x800E0154,0x800E017C)]
    print('turn',turn(),'funds',w(0x800DF38C),'other',w(0x800DF390),'cur base',w(0x800DF404))
    B=ch.read(bs,16*92)
    print('bases', [(i,B[i*92]) for i in range(16) if B[i*92]])
    O=ch.read(ok,200*12)
    print('okes', ' '.join('%d:%d/%d/b%d/c%d/hp%d'%(i,O[i*12],O[i*12+1],O[i*12+2],O[i*12+3],struct.unpack('<h',O[i*12+6:i*12+8])[0]) for i in range(200) if O[i*12]))
    U=ch.read(un,64*22)
    for i in range(64):
        u=U[i*22:i*22+22]
        if u[0]: print(' unit',i,'own',u[0],'task',u[1],'xy',struct.unpack('<hh',u[2:6]),'tgt',struct.unpack('<H',u[6:8])[0],'okes',struct.unpack('<hhh',u[12:18]))
    print(ch.read(pc+25*1152,20).hex())

def form_and_order(order_downs, pick_xy):
    """From the Command menu (End Turn selected): Tactics > Form Unit > Auto, then
    Deploy Units > first unit > order (0 Move, 1 Defend, 2 Capture) > map pick; back to Command."""
    for i in range(4): ch.press('up',hold=8,after=15)
    ch.press('x',hold=8,after=60)                 # Tactics
    ch.press('down',hold=8,after=15); ch.press('x',hold=8,after=90)  # Form Unit
    ch.press('x',hold=8,after=60)                 # help text the first time (otherwise this X does nothing here)
    ch.click(170,198,after=60)                    # Auto
    ch.press('circle',hold=8,after=60)
    ch.press('up',hold=8,after=15); ch.press('x',hold=8,after=60)    # Deploy Units
    ch.press('x',hold=8,after=40)                 # help text
    ch.click(90,74,after=60)                      # first unit
    for i in range(order_downs): ch.press('down',hold=8,after=15)
    ch.press('x',hold=8,after=90)
    ch.click(*pick_xy, after=60)
    ch.press('circle',hold=8,after=40); ch.press('circle',hold=8,after=60)


if __name__ == '__main__' and sys.argv[1:] == ['demo']:
    # From the Command menu at Europa turn 1 (Tactics selected): side A ends its turn,
    # side B forms a unit and orders it to capture A's start base, then End Turn until
    # the game is over (B takes A's undefended base).
    end_turn()
    form_and_order(2, (140, 135))
    for _ in range(12):
        end_turn()
        if b'game over' in open(LOG, 'rb').read():
            break
    state(); print(ch.shot())
