import sys,struct
B=open(sys.argv[1],'rb')
lba,size,out=int(sys.argv[2]),int(sys.argv[3]),sys.argv[4]
d=b''
for i in range((size+2047)//2048):
    B.seek((lba+i)*2352+24); d+=B.read(2048)
open(out,'wb').write(d[:size])
