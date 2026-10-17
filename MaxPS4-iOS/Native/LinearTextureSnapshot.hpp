#pragma once
#include <cerrno>
#include <cstdint>
#include <new>
#include <vector>
namespace maxps4::gpu {
// CPU staging only. Input is explicitly linear RGBA8, never inferred from a
// tiled PS4 surface. This does not submit commands or acknowledge a GPU fence.
struct LinearTextureSnapshot {
    int error=0;
    uint32_t width=0,height=0;
    std::vector<uint8_t> rgba;
};
// The lease pins guest mappings for the complete copy; caller must also stop
// concurrent guest writes. Owned tightly packed rows can outlive that lease.
template<class Lease>
LinearTextureSnapshot snapshotLinearRGBA8(const Lease& lease,uint64_t address,
                                         uint32_t width,uint32_t height,uint32_t pitchBytes){
    constexpr size_t limit=1024*1024;
    const uint64_t row=uint64_t(width)*4;
    if(!width || !height || row>limit || pitchBytes<row || uint64_t(height)>limit/row)
        return {-EINVAL,0,0,{}};
    const uint64_t span=uint64_t(height-1)*pitchBytes+row;
    if(span>limit || !address || address>UINT64_MAX-span)return {-EINVAL,0,0,{}};
    // Validate even padding; partial or protected guest resources are refused
    // before any output allocation/copy is published.
    if(!lease.permitted(address,static_cast<size_t>(span),4))return {-EFAULT,0,0,{}};
    try{
        LinearTextureSnapshot out;out.width=width;out.height=height;
        out.rgba.resize(static_cast<size_t>(row)*height);
        for(uint32_t y=0;y<height;++y)
            if(lease.read_data(address+uint64_t(y)*pitchBytes,out.rgba.data()+size_t(y)*row,static_cast<size_t>(row)))
                return {-EFAULT,0,0,{}};
        return out;
    }catch(const std::bad_alloc&){return {-ENOMEM,0,0,{}};}
}
}
