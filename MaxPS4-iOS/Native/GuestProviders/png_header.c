/* Original, freestanding x86-64 guest implementation. No host ABI or libpng.
 * Header parsing only: no decoder instance, work-memory claim or image output.
 * Audited bounded profile: PNG <=16 MiB, header chunks <=64 KiB before IDAT.
 */
typedef unsigned char u8;
typedef unsigned short u16;
typedef unsigned int u32;
typedef unsigned long long u64;
struct Parse {const u8* bytes;u32 size,reserved;};
struct Info {u32 width,height;u16 color,depth;u32 flags;};
static u32 be(const u8* p){return (u32)p[0]<<24|(u32)p[1]<<16|(u32)p[2]<<8|p[3];}
static u32 crc(const u8* p,u32 n){u32 c=~0u;while(n--){c^=*p++;for(u32 i=0;i<8;i++)c=(c>>1)^((0u-(c&1))&0xedb88320u);}return ~c;}
__attribute__((visibility("hidden"))) u32 png_parse(const struct Parse* param,struct Info* output){
 if(!param || !output || !param->bytes)return 0x80690001;
 if(param->reserved)return 0x80690003;
 const u8* p=param->bytes;u32 size=param->size;
 if(size<33 || size>16*1024*1024 || (u64)p+size<(u64)p)return 0x80690002;
 if(be(p)!=0x89504e47 || be(p+4)!=0x0d0a1a0a || be(p+8)!=13 || be(p+12)!=0x49484452)return 0x80690010;
 if(crc(p+12,17)!=be(p+29))return 0x80690010;
 u32 width=be(p+16),height=be(p+20),depth=p[24],color=p[25],flags=p[28];
 if(!width || !height || width>0x7fffffff || height>0x7fffffff || p[26] || p[27] || flags>1)return 0x80690010;
 u16 space;
 switch(color){
 case 0:space=2;if(depth!=1 && depth!=2 && depth!=4 && depth!=8 && depth!=16)return 0x80690010;break;
 case 2:space=3;if(depth!=8 && depth!=16)return 0x80690010;break;
 case 3:return 0x80690011; /* palette semantics are outside this profile */
 case 4:space=18;if(depth!=8 && depth!=16)return 0x80690010;break;
 case 6:space=19;if(depth!=8 && depth!=16)return 0x80690010;break;
 default:return 0x80690010;
 }
 /* Inspect chunks up to the first IDAT; an IHDR-only fragment is not
  * a complete PNG header. No decompression/image allocation is performed. */
 u32 offset=33,found_idat=0;
 while(offset<size){
  if(size-offset<12)return 0x80690010;
  u32 length=be(p+offset),kind=be(p+offset+4);
  if(length>size-offset-12)return 0x80690010;
  if(kind==0x49444154){found_idat=1;break;}
  if(offset>65536 || length>65536-offset)return 0x80690011;
  if(crc(p+offset+4,length+4)!=be(p+offset+8+length))return 0x80690010;
  if(kind==0x74524e53){
   if(flags&2)return 0x80690010;
   if((color==0 && length!=2)||(color==2 && length!=6)||(color==3 && (!length || length>256))||color==4||color==6)return 0x80690010;
   flags|=2;
  }else if(kind==0x504c5445 || !(p[offset+4]&32))return 0x80690011;
  offset+=length+12;
 }
 if(!found_idat)return 0x80690010;
 /* Probe every output byte before publishing fields, including unaligned and
  * crossing-page outputs. FEX memory callbacks enforce guest W^X/permissions. */
 for(u32 i=0;i<16;i++)__atomic_fetch_or((volatile u8*)output+i,0,__ATOMIC_SEQ_CST);
 output->width=width;output->height=height;output->color=space;output->depth=depth;output->flags=flags;
 return 0;
}
__attribute__((visibility("hidden"))) u32 png_create(void){return 0x80690011;}
__attribute__((visibility("hidden"))) u32 png_decode(void){return 0x80690004;}
__attribute__((visibility("hidden"))) u32 png_query(void){return 0x80690011;}
