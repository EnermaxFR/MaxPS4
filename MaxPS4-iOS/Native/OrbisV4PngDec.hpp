#pragma once
#include "OrbisV4VirtualPRX.hpp"
namespace maxps4 {
struct OrbisV4PngDec {
    static constexpr const char* path="libScePngDec.prx";
    static constexpr uint64_t bias=0x1080000;
    static std::vector<uint8_t> provider(){
#include "GuestProviders/png_header.inc"
        return OrbisV4VirtualPRX::build("libScePngDec",{std::begin(png_code),std::end(png_code)},4,{
            {"m0uW+8pFyaw",png_create_offset,png_create_size},
            {"WC216DD3El4",png_decode_offset,png_decode_size},
            {"U6h4e5JRPaQ",png_parse_offset,png_parse_size},
            {"-6srIGbLTIU",png_query_offset,png_query_size}});
    }
};
}
