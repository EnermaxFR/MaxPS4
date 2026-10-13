#include "../Native/OrbisLinker.hpp"
#include "../Native/OrbisLinkerBridge.h"
#include "OrbisLinkerFixture.h"
#include <iostream>
#include <cstdlib>
#include <fstream>
using Linker = maxps4::OrbisLinker;
using namespace orbis_fixture;
static size_t checks = 0;
static void check(bool ok, const char* message) { ++checks; if (!ok) { std::cerr << message << '\n'; std::exit(1); } }
static uint64_t value(const Linker::Image& image, uint64_t address) {
    const auto* p = image.translate(address,8,4); check(p != nullptr,"read relocation");
    uint64_t v = 0; for (size_t i=0;i<8;++i) v |= uint64_t(p[i])<<(i*8); return v;
}
int main(int argc, char** argv) {
    auto provider = make(true), consumer = make(false);
    if (argc == 2) {
        const std::pair<const char*, const std::vector<uint8_t>*> fixtures[] = {{"provider.prx", &provider}, {"app.elf", &consumer}};
        for (const auto& fixture : fixtures) {
            std::ofstream file(std::string(argv[1]) + "/" + fixture.first, std::ios::binary);
            file.write(reinterpret_cast<const char*>(fixture.second->data()), static_cast<std::streamsize>(fixture.second->size()));
            check(bool(file), "write synthetic fixture");
        }
    }
    auto input = [](const std::string& path, const std::vector<uint8_t>& b, uint64_t bias, std::vector<std::string> deps) {
        return Linker::Input{{{path,std::move(deps)},b.data(),b.size()},bias};
    };
    auto batch = [&] { return std::vector<Linker::Input>{input("app.elf",consumer,0x100000,{"provider.prx"}),input("provider.prx",provider,0x200000,{})}; };
    Linker::Image image; std::string diagnostic;
    check(Linker::link(batch(),{},image,diagnostic),diagnostic.c_str());
    check(image.modules.size()==2 && image.modules[0].path=="provider.prx", "dependency first");
    check(value(image,0x102000)==0x201000,"NID JUMP_SLOT resolved");
    check(image.modules[1].entry==0x101000 && image.modules[0].entry==0,"guest entries");
    const auto original = consumer;
    auto reversed = batch(); std::reverse(reversed.begin(),reversed.end()); Linker::Image reordered;
    check(Linker::link(reversed,{},reordered,diagnostic),"reversed input links");
    check(reordered.modules[0].path==image.modules[0].path && value(reordered,0x102000)==value(image,0x102000),"deterministic order and bytes");
    auto reject = [&](std::vector<Linker::Input> inputs, const std::string& expected, size_t budget = 64*1024*1024) {
        check(!Linker::link(inputs,{},image,diagnostic,budget),"expected rejection");
        check(diagnostic.find(expected)!=std::string::npos,diagnostic.c_str());
        check(image.modules.size()==2 && value(image,0x102000)==0x201000,"failed link preserves complete previous image");
    };
    consumer[0x601+std::string("FixtureLibrary").size()+1+std::string("FixtureModule").size()+1] = 'B';
    reject(batch(),"unresolved NID"); consumer=original;
    auto duplicate = batch(); duplicate.push_back(input("duplicate.prx",provider,0x300000,{})); reject(duplicate,"duplicate or ambiguous export");
    auto cycle = batch(); cycle[1].file.request.dependencies={"app.elf"}; reject(cycle,"circulaire");
    auto missing = batch(); missing[0].file.request.dependencies={"absent.prx"}; reject(missing,"manquante");
    auto undeclared = batch(); undeclared[0].file.request.dependencies.clear(); reject(undeclared,"missing explicit dependency");
    put(consumer,0x900,0x20fc); reject(batch(),"relocation target"); consumer=original;
    put(consumer,0x908,(uint64_t{1}<<32)|99); reject(batch(),"unsupported x86-64 relocation"); consumer=original;
    put(consumer,0x908,(uint64_t{99}<<32)|7); reject(batch(),"invalid relocation symbol"); consumer=original;
    reject(batch(),"memory budget",1023); reject(batch(),"budget exceeds",65*1024*1024);
    put(consumer,64+56+4,4,4); reject(batch(),"relocation target"); consumer=original;
    put(consumer,64+4,7,4); reject(batch(),"writable executable"); consumer=original;
    put(consumer,0x900,0x1000); reject(batch(),"relocation target"); consumer=original;
    auto truncated = batch(); truncated[0].file.size=63; reject(truncated,"chargeur");
    consumer[0]=0; reject(batch(),"ELF Orbis"); consumer=original;
    setTag(consumer,0x61000035,0x5ff); reject(batch(),"SCE table out of bounds"); consumer=original;
    setTag(consumer,0x6100003b,16); reject(batch(),"symbol entry size"); consumer=original;
    setTag(consumer,0x6100003f,47); reject(batch(),"symbol table budget or size"); consumer=original;
    put(consumer,0x818,0xffffffff,4); reject(batch(),"string offset"); consumer=original;
    for (size_t p=0x400;p<0x600;p+=16) { put(consumer,p,0x61000035); put(consumer,p+8,0); }
    reject(batch(),"duplicate dynamic tag"); consumer=original;
    // Duplicate symbol within one provider is just as ambiguous as across modules.
    setTag(provider,0x6100003f,72); std::copy(provider.begin()+0x818,provider.begin()+0x830,provider.begin()+0x830);
    reject(batch(),"duplicate or ambiguous export"); provider=make(true);
    // A later invalid relocation must not expose an earlier valid write.
    setTag(consumer,0x61000031,48); put(consumer,0x918,0x2008); put(consumer,0x920,(uint64_t{1}<<32)|99);
    reject(batch(),"unsupported x86-64 relocation");
    check(std::all_of(consumer.begin()+0x300,consumer.begin()+0x400,[](uint8_t b){return b==0;}),"caller file never modified"); consumer=original;
    // ABI arithmetic, including signed negative addends and ignored JUMP_SLOT addend.
    for (uint32_t type : {1U,6U,7U,8U}) {
        put(consumer,0x908,(type==8 ? 0 : uint64_t{1}<<32)|type); put(consumer,0x910,UINT64_MAX-7);
        Linker::Image result; check(Linker::link(batch(),{},result,diagnostic),diagnostic.c_str());
        check(value(result,0x102000)==(type==1 ? 0x200ff8 : type==8 ? 0xffff8 : 0x201000),"ABI relocation formula");
    }
    consumer=original;
    put(consumer,0x908,(uint64_t{1}<<32)|1); put(consumer,0x910,uint64_t{1}<<63); reject(batch(),"underflow");
    consumer=original;
    // RELRO is protected after relocation, not left writable for the backend.
    put(consumer,64+56,0x61000010,4);
    Linker::Image relro; check(Linker::link(batch(),{},relro,diagnostic), "RELRO links");
    check(value(relro,0x102000)==0x201000 && !relro.translate(0x102000,8,2), "RELRO final read-only permissions"); consumer=original;
    // Two partially overlapping writes are rejected before either is applied.
    setTag(consumer,0x61000031,48); put(consumer,0x918,0x2004); put(consumer,0x920,(uint64_t{1}<<32)|7);
    reject(batch(), "overlapping relocation writes"); consumer=original;
    // SCE PLT RELA table has a separately verified DT_RELA encoding.
    put(consumer,0x470,0x61000029); put(consumer,0x480,0x6100002d); put(consumer,0x490,0x6100002b); put(consumer,0x498,7);
    Linker::Image plt; check(Linker::link(batch(),{},plt,diagnostic) && value(plt,0x102000)==0x201000, "SCE PLT RELA");
    put(consumer,0x498,17); reject(batch(), "relocation table encoding"); consumer=original;
    // Exact descriptor versions are part of the key; no guessed compatibility.
    setTag(consumer,0x61000015,uint64_t{1}|(uint64_t{2}<<32)|(uint64_t{1}<<48)); reject(batch(), "unresolved NID"); consumer=original;
    put(consumer,0x460,0xdeadbeef); reject(batch(), "unsupported dynamic tag"); consumer=original;
    put(consumer,64+3*56+8,UINT64_MAX); reject(batch(), "dynamic metadata out of bounds"); consumer=original;
    // A PRX entry in zero-filled memory is not admitted as file-backed code.
    put(provider,24,0x1100); put(provider,64+40,0x200); reject(batch(), "invalid module entry"); provider=make(true);
    auto overlapping=batch(); overlapping[1].bias=0x100000; reject(overlapping,"overlapping guest modules");
    auto overflow=batch(); overflow[1].bias=UINT64_MAX-0x800; reject(overflow,"guest address overflow");
    auto names=batch(); names[1].file.request.logicalPath="../provider.prx"; reject(names,"chemin");
    // HLE is explicit, type/version-specific and maps to a guest code trampoline.
    auto noExport=make(true); noExport[0x81c]=0x02;
    auto hleBatch=batch(); hleBatch[1]=input("provider.prx",noExport,0x200000,{});
    Linker::Identity id{"AAAAAAAAAAA","FixtureLibrary","FixtureModule",1,0x100,2};
    Linker::Image hleImage;
    check(Linker::link(hleBatch,{{id,0x201000}},hleImage,diagnostic),"explicit HLE guest trampoline");
    check(value(hleImage,0x102000)==0x201000,"HLE relocation");
    check(!Linker::link(hleBatch,{{id,0x202000}},hleImage,diagnostic),"HLE data target refused");
    id.libraryVersion=2; check(!Linker::link(hleBatch,{{id,0x201000}},hleImage,diagnostic),"HLE version mismatch unresolved");
    check(consumer==original,"source unchanged after successful links");
    const char* deps[]={"provider.prx"};
    maxps4_orbis_module native[]={{"app.elf",consumer.data(),consumer.size(),0x100000,deps,1},{"provider.prx",provider.data(),provider.size(),0x200000,nullptr,0}};
    maxps4_orbis_image* handle=nullptr; char report[256];
    check(maxps4_orbis_link(native,2,1024,&handle,report,sizeof(report))==0,report);
    check(maxps4_orbis_mapping_count(handle)==4,"native mappings"); maxps4_orbis_mapping mapping{};
    check(maxps4_orbis_get_mapping(handle,0,&mapping)==0 && mapping.address==0x201000 && mapping.permissions==5,"guest mapping permissions");
    uint64_t entry=0; check(maxps4_orbis_entry(handle,"app.elf",&entry)==0 && entry==0x101000,"native entry");
    uint8_t copied[8]{}; check(maxps4_orbis_read(handle,0x102000,copied,8)==0 && copied[2]==0x20,"native guest read");
    std::fill(std::begin(copied),std::end(copied),0xa5);
    check(maxps4_orbis_read(handle,0x1020ff,copied,8)<0 && copied[0]==0xa5 && copied[7]==0xa5,"failed native read preserves output");
    check(maxps4_orbis_get_mapping(handle,99,&mapping)<0,"invalid mapping index");
    maxps4_orbis_close(handle); handle=nullptr;
    native[0].size=1;
    check(maxps4_orbis_link(native,2,1024,&handle,report,sizeof(report))<0 && !handle && report[0],"failed native transaction");
    std::cout << "Orbis linker: " << checks << " checks passed\n";
}
