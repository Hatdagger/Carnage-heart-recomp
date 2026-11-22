import sys,struct
B=open(sys.argv[1],'rb')
def sec(n):
    B.seek(n*2352+24); return B.read(2048)
def rd(lba,size):
    out=b''
    for i in range((size+2047)//2048): out+=sec(lba+i)
    return out[:size]
pvd=sec(16); root=pvd[156:190]
def walk(lba,size,path):
    d=rd(lba,size); i=0
    while i<len(d):
        l=d[i]
        if l==0: i=(i//2048+1)*2048; continue
        e=d[i:i+l]; elba,esz=struct.unpack('<I',e[2:6])[0],struct.unpack('<I',e[10:14])[0]
        fl=e[25]; nl=e[32]; name=e[33:33+nl]
        if name not in (b'\0',b'\1'):
            n=name.decode('latin1')
            if fl&2: walk(elba,esz,path+n+'/')
            else: print('%8d %10d %s%s'%(elba,esz,path,n))
        i+=l
walk(struct.unpack('<I',root[2:6])[0],struct.unpack('<I',root[10:14])[0],'/')
