import sys; import os; sys.path.insert(0, os.path.dirname(os.path.abspath(__file__))); import xref, fx
J={}
for i,w in enumerate(xref.words):
    if w>>26==3: J.setdefault((w&0x3ffffff)<<2|0x80000000,set()).add(xref.func_of(xref.LOAD+4*i))
for a in sys.argv[1:]:
    f=int(a,16)
    print('%08x size %d calls [%s] callers [%s]'%(f,fx.size(f),' '.join('%08x'%c for c in fx.calls(f)),' '.join('%08x'%c for c in sorted(J.get(f,())))))
