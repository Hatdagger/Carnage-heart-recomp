import sys; import os; sys.path.insert(0, os.path.dirname(os.path.abspath(__file__))); import mdis, xref
def calls(f):
    i=xref.funcs.index(f); end=xref.funcs[i+1]
    return sorted({(w&0x3ffffff)<<2|0x80000000 for pc in range(f,end,4) for w in [xref.words[(pc-xref.LOAD)//4]] if w>>26==3})
def dis(f):
    i=xref.funcs.index(f); end=xref.funcs[i+1]
    return '\n'.join('%08x  %s'%(pc, mdis.dis(pc, xref.words[(pc-xref.LOAD)//4])) for pc in range(f,end,4))
def size(f):
    i=xref.funcs.index(f); return xref.funcs[i+1]-f
if __name__=='__main__':
    for a in sys.argv[2:]:
        f=int(a,16)
        if sys.argv[1]=='calls': print('%08x (%d bytes) calls: %s'%(f,size(f),' '.join('%08x'%c for c in calls(f))))
        else: print(dis(f))
