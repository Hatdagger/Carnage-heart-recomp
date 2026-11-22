"""Run one End Turn from the current state, capture the battle, report results."""
import os,sys,struct; sys.path.insert(0, os.path.dirname(os.path.abspath(__file__))); import ch
def units():
    d=ch.read(0x80134b6c,64*22); return {i:struct.unpack('<BBhhHHBBhhhHH',d[22*i:22*i+22]) for i in range(64) if d[22*i]}
def oke(k):
    o=ch.read(0x801356ac+12*k,12); return dict(owner=o[0],card=o[3],kills=struct.unpack('<h',o[4:6])[0],hp=struct.unpack('<h',o[6:8])[0],c8=struct.unpack('<h',o[8:10])[0],main=o[10],sub=o[11])
def turn(): return struct.unpack('<I', ch.read(0x800df37c, 4))[0]
u=units(); print('before', u)
watch=sorted({k for v in u.values() for k in v[8:11] if k>=0})
cards0=ch.read(0x8012C97C,28*1152)
t0=turn(); ch.press('x',hold=6,after=20)
for i in range(16):
    ch.wait(90); ch.shot(os.path.join(ch.OUT,'c%02d.png'%i))
for t in range(20):
    ch.wait(300)
    if struct.unpack('<i',ch.read(0x800DF3D0,4))[0]==0: break
ch.wait(600); ch.shot(os.path.join(ch.OUT,'end.png'))
print('after ', units())
for k in watch: print(' oke',k,oke(k))
b=ch.read(0x801350ec,16*92); print('base owners',[b[92*i] for i in range(3)],'turn',turn())
print('player cards unchanged:', ch.read(0x8012C97C,28*1152)==cards0)
from PIL import Image
ims=[Image.open(os.path.join(ch.OUT,'c%02d.png'%i)).resize((256,120)) for i in range(16)]
W=Image.new('RGB',(1024,480))
for i,im in enumerate(ims): W.paste(im,((i%4)*256,(i//4)*120))
W.save(os.path.join(ch.OUT,'grid.png'))
