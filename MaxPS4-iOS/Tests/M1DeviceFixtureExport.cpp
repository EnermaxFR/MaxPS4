#include "OrbisM1Fixture.h"
#include <filesystem>
#include <fstream>
#include <iostream>
int main(int argc,char** argv) {
    static_assert(maxps4::OrbisM1::thread_base==0x400400000ULL,"iPhone low guest profile required");
    static_assert(orbis_m1_fixture::entry==0x400004000ULL,"historical fixture entry");
    if (argc!=2) return 2;
    const std::filesystem::path directory(argv[1]);
    std::filesystem::create_directories(directory);
    auto save=[&](const char* name,const std::vector<uint8_t>& bytes) {
        std::ofstream file(directory/name,std::ios::binary);
        file.write(reinterpret_cast<const char*>(bytes.data()),bytes.size());
        return bool(file);
    };
    if (!save("m1-two-threads-three-tls-modules.elf",orbis_m1_fixture::make(17,true,true))) return 1;
    for (uint32_t value:{83U,129U}) {
        auto bytes=orbis_m1_fixture::make(value,false);
        maxps4_ps4_synthetic::put(bytes,0xfe18,16,2);
        if (!save(value==83 ? "tls-83.prx" : "tls-129.prx",bytes)) return 1;
    }
    std::cout<<"PASS generated iPhone M1 fixture profile: entry=0x400004000, thread_base=0x400400000; no CPU execution\n";
}
