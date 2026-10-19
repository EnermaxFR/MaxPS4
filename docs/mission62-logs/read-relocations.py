import struct,sys
d=open(sys.argv[1],'rb').read()
n=struct.unpack_from('<H',d,0x18)[0]
ents=[struct.unpack_from('<QQQQ',d,0x20+i*32) for i in range(n)]
e=0x20+n*32
phoff,=struct.unpack_from('<Q',d,e+0x20); phnum,=struct.unpack_from('<H',d,e+0x38)
ph=[struct.unpack_from('<IIQQQQQQ',d,e+phoff+i*56) for i in range(phnum)]
def fileoff(off):
    for i,(t,fl,o,va,pa,fs,ms,al) in enumerate(ph):
        for p,so,fsz,msz in ents:
            if (p>>20)&0xffff==i and p&0x800 and o<=off<o+fs: return so+off-o
dynlib=[x for x in ph if x[0]==0x61000000][0]; dyn=[x for x in ph if x[0]==2][0]
base=fileoff(dynlib[2]); dynoff=fileoff(dyn[2]); tags={}
for k in range(dyn[5]//16):
    t,v=struct.unpack_from('<qQ',d,dynoff+k*16); tags.setdefault(t,[]).append(v)
ra=base+tags[0x6100002f][0]; rs=tags[0x61000031][0]
rel={}
for k in range(rs//24):
    off,info,add=struct.unpack_from('<QQq',d,ra+k*24)
    if info&0xffffffff==8: rel[off]=add
import json
open('docs/mission62-logs/relocations.json','w').write(json.dumps(rel))
text=open('docs/mission62-logs/text.bin','rb').read()
for i in range(256):
 o=0x14d180+i*40; a=rel.get(o,0); fn=rel.get(o-32,0)
 if a and a<len(text):print(i,text[a:text.find(bytes([0]),a)].decode('ascii',errors='replace'),hex(fn))
