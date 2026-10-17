#include "../Native/LinearTextureSnapshot.hpp"
#include "../Native/maxps4_orbis_v4_translation.h"
#include <array>
#include <cassert>
#include <cstdio>
int main(){
 using namespace maxps4;using namespace maxps4::gpu;
 OrbisV4Translation owner([]{});const size_t page=owner.page_size();const uint64_t base=0x500000;
 assert(owner.reserve(base,3*page)==0 && owner.map(base,page,6)==0 && owner.map(base+page,page,6)==0);
 const uint64_t image=base+page-4;
 const std::array<uint8_t,20> input{1,2,3,4,5,6,7,8,99,99,99,99,9,10,11,12,13,14,15,16};
 assert(owner.write(image,input.data(),input.size())==0);
 LinearTextureSnapshot result;
 {auto lease=owner.lease();result=snapshotLinearRGBA8(lease,image,2,2,12);
  const std::vector<uint8_t> expected{1,2,3,4,5,6,7,8,9,10,11,12,13,14,15,16};
  assert(!result.error && result.width==2 && result.height==2 && result.rgba==expected);
  assert(snapshotLinearRGBA8(lease,image,0,2,12).error==-EINVAL);
  assert(snapshotLinearRGBA8(lease,image,2,2,7).error==-EINVAL);
  assert(snapshotLinearRGBA8(lease,UINT64_MAX-4,2,2,12).error==-EINVAL);
  assert(snapshotLinearRGBA8(lease,image,UINT32_MAX,UINT32_MAX,UINT32_MAX).error==-EINVAL);
  auto fault=snapshotLinearRGBA8(lease,base+2*page-4,2,2,12);assert(fault.error==-EFAULT && fault.rgba.empty());}
 assert(owner.protect(base+page,page,0)==0);
 {auto lease=owner.lease();assert(snapshotLinearRGBA8(lease,image,2,2,12).error==-EFAULT);}
 owner.close();assert(result.rgba[15]==16);
 std::puts("PASS owned linear RGBA8 staging: split guest mappings, pitch/padding, overflow/budget/hole/permissions, lifetime; no GPU submission");
}
