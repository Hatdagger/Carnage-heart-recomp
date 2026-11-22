import sys
a=open(sys.argv[1],'rb').read(); b=open(sys.argv[2],'rb').read()
lo=int(sys.argv[3],16)-0x80000000; hi=int(sys.argv[4],16)-0x80000000
i=lo
while i<hi:
  if a[i]!=b[i]:
    j=i
    while j<hi and a[j:j+4]!=b[j:j+4]: j+=1
    print('%08x: %s -> %s'%(0x80000000+i,a[i:j].hex(),b[i:j].hex())); i=j
  i+=1
