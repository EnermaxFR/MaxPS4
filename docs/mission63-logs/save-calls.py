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
import csv,re
names={x['nid']:x['name'] for x in csv.DictReader(open('docs/mission44-imports.tsv'),delimiter='\t')}
slots={}
for k in range(tags[0x6100002d][0]//24):
 off,info,add=struct.unpack_from('<QQq',d,base+tags[0x61000029][0]+k*24)
 stname,=struct.unpack_from('<I',d,base+tags[0x61000039][0]+(info>>32)*24)
 start=base+tags[0x61000035][0]+stname
 name=d[start:d.index(bytes([0]),start)].decode().split('#')[0]
 slots[off]=names.get(name,name)
text=open('docs/mission62-logs/text.bin','rb').read()
for a in range(0x10e000,0x10ff00):
 if text[a:a+2]==bytes.fromhex('ff25'):
  slot=a+6+struct.unpack_from('<i',text,a+2)[0]
  name=slots.get(slot,'')
  if 'SaveData' in name:
   print(hex(a),name)
   for line in open('docs/mission62-logs/eboot-disassembly.txt'):
    if re.search(r'(call|jmp) +0x%x$'%a,line.strip()):print(' ',line.strip())
