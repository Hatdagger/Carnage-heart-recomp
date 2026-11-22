import sys,struct
import os; sys.path.insert(0, os.path.dirname(os.path.abspath(__file__))); import xref
d=xref.raw[0x800:]
for a in sys.argv[1:]:
    v=struct.pack('<I',int(a,16)); i=d.find(v); hits=[]
    while i>=0: hits.append('%08X'%(0x80010000+i)); i=d.find(v,i+1)
    print(a,hits)
