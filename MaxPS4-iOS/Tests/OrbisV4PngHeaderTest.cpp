#include <array>
#include <cassert>
#include <cstdio>
#include <cstring>
extern "C" {
#include "../Native/GuestProviders/png_header.c"
}
int main(){
    std::array<u8,68> png{137,80,78,71,13,10,26,10,0,0,0,13,73,72,68,82,0,0,0,1,0,0,0,1,8,6,0,0,0,31,21,196,137,0,0,0,11,73,68,65,84,120,156,99,96,0,2,0,0,5,0,1,122,94,171,63,0,0,0,0,73,69,78,68,174,66,96,130};
    Parse param{png.data(),u32(png.size()),0};Info out{},sentinel;std::memset(&sentinel,0xa5,sizeof(sentinel));
    assert(png_parse(&param,&out)==0 && out.width==1 && out.height==1 && out.color==19 && out.depth==8 && !out.flags);
    for(unsigned size=0;size<33;++size){param.size=size;out=sentinel;
        assert(png_parse(&param,&out)==0x80690002 && !std::memcmp(&out,&sentinel,sizeof(out)));}
    param.size=u32(png.size());
    for(size_t i=0;i<33;++i){png[i]^=1;out=sentinel;
        assert(png_parse(&param,&out)!=0 && !std::memcmp(&out,&sentinel,sizeof(out)));png[i]^=1;}
    param.size=33;assert(png_parse(&param,&out)==0x80690010);param.size=u32(png.size());
    param.reserved=1;assert(png_parse(&param,&out)==0x80690003);param.reserved=0;
    assert(png_parse(nullptr,&out)==0x80690001 && png_parse(&param,nullptr)==0x80690001);
    assert(png_create()==0x80690011 && png_query()==0x80690011 && png_decode()==0x80690004);
    // Independent vectors generated with Python zlib.crc32 (PNG wire format).
    {std::array<u8,68> bytes{137,80,78,71,13,10,26,10,0,0,0,13,73,72,68,82,0,0,0,1,0,0,0,1,8,0,0,0,0,58,126,155,85,0,0,0,11,73,68,65,84,120,156,99,96,0,2,0,0,5,0,1,122,94,171,63,0,0,0,0,73,69,78,68,174,66,96,130};Parse p{bytes.data(),u32(bytes.size()),0};out=sentinel;
     assert(png_parse(&p,&out)==0x0);assert(out.color==2 && out.flags==0);}
    {std::array<u8,68> bytes{137,80,78,71,13,10,26,10,0,0,0,13,73,72,68,82,0,0,0,1,0,0,0,1,8,2,0,0,0,144,119,83,222,0,0,0,11,73,68,65,84,120,156,99,96,0,2,0,0,5,0,1,122,94,171,63,0,0,0,0,73,69,78,68,174,66,96,130};Parse p{bytes.data(),u32(bytes.size()),0};out=sentinel;
     assert(png_parse(&p,&out)==0x0);assert(out.color==3 && out.flags==0);}
    {std::array<u8,68> bytes{137,80,78,71,13,10,26,10,0,0,0,13,73,72,68,82,0,0,0,1,0,0,0,1,8,4,0,0,0,181,28,12,2,0,0,0,11,73,68,65,84,120,156,99,96,0,2,0,0,5,0,1,122,94,171,63,0,0,0,0,73,69,78,68,174,66,96,130};Parse p{bytes.data(),u32(bytes.size()),0};out=sentinel;
     assert(png_parse(&p,&out)==0x0);assert(out.color==18 && out.flags==0);}
    {std::array<u8,68> bytes{137,80,78,71,13,10,26,10,0,0,0,13,73,72,68,82,0,0,0,1,0,0,0,1,8,6,0,0,0,31,21,196,137,0,0,0,11,73,68,65,84,120,156,99,96,0,2,0,0,5,0,1,122,94,171,63,0,0,0,0,73,69,78,68,174,66,96,130};Parse p{bytes.data(),u32(bytes.size()),0};out=sentinel;
     assert(png_parse(&p,&out)==0x0);assert(out.color==19 && out.flags==0);}
    {std::array<u8,68> bytes{137,80,78,71,13,10,26,10,0,0,0,13,73,72,68,82,0,0,0,1,0,0,0,1,8,3,0,0,0,40,203,52,187,0,0,0,11,73,68,65,84,120,156,99,96,0,2,0,0,5,0,1,122,94,171,63,0,0,0,0,73,69,78,68,174,66,96,130};Parse p{bytes.data(),u32(bytes.size()),0};out=sentinel;
     assert(png_parse(&p,&out)==0x80690011);assert(!std::memcmp(&out,&sentinel,sizeof(out)));}
    {std::array<u8,82> bytes{137,80,78,71,13,10,26,10,0,0,0,13,73,72,68,82,0,0,0,1,0,0,0,1,8,0,0,0,0,58,126,155,85,0,0,0,2,116,82,78,83,0,0,118,147,205,56,0,0,0,11,73,68,65,84,120,156,99,96,0,2,0,0,5,0,1,122,94,171,63,0,0,0,0,73,69,78,68,174,66,96,130};Parse p{bytes.data(),u32(bytes.size()),0};out=sentinel;
     assert(png_parse(&p,&out)==0x0);assert(out.color==2 && out.flags==2);}
    {std::array<u8,96> bytes{137,80,78,71,13,10,26,10,0,0,0,13,73,72,68,82,0,0,0,1,0,0,0,1,8,0,0,0,0,58,126,155,85,0,0,0,2,116,82,78,83,0,0,118,147,205,56,0,0,0,2,116,82,78,83,0,0,118,147,205,56,0,0,0,11,73,68,65,84,120,156,99,96,0,2,0,0,5,0,1,122,94,171,63,0,0,0,0,73,69,78,68,174,66,96,130};Parse p{bytes.data(),u32(bytes.size()),0};out=sentinel;
     assert(png_parse(&p,&out)==0x80690010);assert(!std::memcmp(&out,&sentinel,sizeof(out)));}
    {std::array<u8,83> bytes{137,80,78,71,13,10,26,10,0,0,0,13,73,72,68,82,0,0,0,1,0,0,0,1,8,0,0,0,0,58,126,155,85,0,0,0,3,80,76,84,69,0,0,0,167,122,61,218,0,0,0,11,73,68,65,84,120,156,99,96,0,2,0,0,5,0,1,122,94,171,63,0,0,0,0,73,69,78,68,174,66,96,130};Parse p{bytes.data(),u32(bytes.size()),0};out=sentinel;
     assert(png_parse(&p,&out)==0x80690011);assert(!std::memcmp(&out,&sentinel,sizeof(out)));}
    std::puts("PASS PNG known IHDR/CRC, every truncation and byte corruption, untouched outputs and unavailable decoder");
}
