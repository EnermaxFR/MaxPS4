#include "../Native/OrbisLinker.hpp"
#include "../Native/OrbisLinkerBridge.h"
#include "OrbisLinkerFixture.h"
#include "OrbisEHFrameFixture.h"
#include "OrbisDynamicTagsFixture.h"
#include "OrbisSymbolsFixture.h"
#include <iostream>
#include <cstdlib>
#include <fstream>
using Linker = maxps4::OrbisLinker;
using namespace orbis_fixture;
static size_t checks = 0;
static void check(bool ok, const char* message) { ++checks; if (!ok) { std::cerr << "check " << checks << ": " << message << '\n'; std::exit(1); } }
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
        check(!Linker::link(inputs,{},image,diagnostic,budget),("expected rejection: "+expected).c_str());
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
    reject({},"invalid module count");
    auto tooMany=std::vector<Linker::Input>(129,batch()[0]); reject(tooMany,"invalid module count");
    auto tooManyDeps=batch(); tooManyDeps[0].file.request.dependencies.resize(129,"provider.prx"); reject(tooManyDeps,"request budget");
    auto giantPath=batch(); giantPath[0].file.request.dependencies={std::string(256,'a')}; reject(giantPath,"path budget");
    auto giantFile=batch(); giantFile[0].file.size=SIZE_MAX; reject(giantFile,"invalid ELF file size");
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
    // -ENOEXEC at the C bridge retains a machine-stable SCE/PRX/NID stage.
    auto notELF = consumer; std::memcpy(notELF.data(),"\177CNT",4);
    auto wrongContainer = batch(); wrongContainer[0].file.bytes = notELF.data();
    Linker::Image untouched; Linker::Report containerReport;
    check(!Linker::link(wrongContainer,{},untouched,diagnostic,4096,&containerReport),"CNT cannot enter ELF staging");
    check(diagnostic.find("[ORBIS.ELF]")!=std::string::npos &&
          diagnostic.find("[ELF.CONTAINER]")!=std::string::npos && untouched.modules.empty() &&
          !containerReport.committed,"container refusal has stage and no partial image");
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
    // NONE may contain unused, unmapped offsets/symbols and must not write.
    setTag(consumer,0x61000031,72);
    put(consumer,0x918,UINT64_MAX); put(consumer,0x920,uint64_t{UINT32_MAX}<<32);
    put(consumer,0x928,UINT64_MAX);
    put(consumer,0x930,0x2000); put(consumer,0x938,0); put(consumer,0x940,123);
    Linker::Image none; Linker::Report noneReport;
    check(Linker::link(batch(),{},none,diagnostic,4096,&noneReport),diagnostic.c_str());
    check(value(none,0x102000)==0x201000 && noneReport.ignoredRelocations==2 &&
        noneReport.relocations==1 && noneReport.validatedRelocations==1,"NONE has no write or symbol lookup");
    check(noneReport.text().find("aucune écriture) : 2")!=std::string::npos,"NONE distinct from applied relocations");
    // A real invalid relocation after NONE still rolls back the transaction.
    put(consumer,0x938,99);
    check(!Linker::link(batch(),{},none,diagnostic,4096,&noneReport) &&
        value(none,0x102000)==0x201000 && !noneReport.committed,"NONE preserves late failure rollback");
    consumer=original;
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
    setTag(consumer,0x61000015,uint64_t{1}|(uint64_t{2}<<32)|(uint64_t{1}<<48)); reject(batch(), "PRX library"); consumer=original;
    put(consumer,0x460,0xdeadbeef); reject(batch(), "unsupported dynamic tag"); consumer=original;
    put(consumer,64+3*56+8,UINT64_MAX); reject(batch(), "chargeur"); consumer=original;
    // A PRX entry in zero-filled memory is not admitted as file-backed code.
    put(provider,24,0x1100); put(provider,64+40,0x200); reject(batch(), "invalid module entry"); provider=make(true);
    put(consumer,64+2*56+32,160); reject(batch(), "unterminated dynamic table"); consumer=original;
    put(consumer,64+2*56+32,17); reject(batch(), "dynamic metadata out of bounds"); consumer=original;
    setTag(consumer,0x61000037,1); reject(batch(), "string offset"); consumer=original;
    // Missing descriptor ID, invalid NID alphabet and export outside mapped memory.
    const size_t symbolName = 0x601 + std::string("FixtureLibrary").size()+1+std::string("FixtureModule").size()+1;
    consumer[symbolName]='!'; reject(batch(), "invalid NID alphabet"); consumer=original;
    consumer[symbolName+12]='C'; reject(batch(), "missing library/module ID"); consumer=original;
    put(provider,64+40,0x200); put(provider,0x820,0x1100);
    reject(batch(),"function export outside file-backed code"); provider=make(true);
    put(provider,64+40,0x200); put(provider,0x820,0x10ff); put(provider,0x828,2);
    reject(batch(),"function export outside file-backed code"); provider=make(true);
    put(provider,0x820,0x3000); reject(batch(), "export outside valid segment"); provider=make(true);
    put(consumer,64+3*56,7,4); reject(batch(), "TLS not supported"); consumer=original;
    put(consumer,64+56+40,33*1024*1024); reject(batch(), "chargeur"); consumer=original;
    // Overflow in S+A, independently of segment-address overflow.
    put(consumer,0x908,(uint64_t{1}<<32)|1); put(consumer,0x910,INT64_MAX);
    auto addOverflow=batch(); addOverflow[1].bias=UINT64_MAX-0x10000;
    reject(addOverflow, "non-canonical"); consumer=original;
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
    check(!Linker::link(hleBatch,{{id,0x201000},{id,0x201000}},hleImage,diagnostic) && diagnostic.find("ambiguous")!=std::string::npos,"duplicate HLE rejected");
    check(!Linker::link(batch(),{{id,0x201000}},hleImage,diagnostic),"HLE/export collision rejected");
    check(!Linker::link(hleBatch,{{id,0x202000}},hleImage,diagnostic),"HLE data target refused");
    id.libraryVersion=2; check(!Linker::link(hleBatch,{{id,0x201000}},hleImage,diagnostic),"HLE version mismatch unresolved");
    // Regression: unused dependency descriptors must not silently disappear.
    put(consumer,0x81c,0x02,1); put(consumer,0x81e,1,2); put(consumer,0x820,0x1000); put(consumer,0x828,1);
    setTag(consumer,0x61000031,0);
    auto unused = batch(); unused[0].file.request.dependencies.clear();
    reject(unused,"missing explicit dependency"); consumer=original;
    // Multiple providers, even with an unreferenced second module identity.
    auto second = provider;
    const size_t moduleOffset = 0x601 + std::string("FixtureLibrary").size()+1;
    second[moduleOffset] = 'S'; second[0x601] = 'S';
    consumer[0x650] = 'S';
    const std::string secondName = "SixtureModule";
    std::copy(secondName.begin(),secondName.end(),consumer.begin()+0x650); consumer[0x650+secondName.size()]=0;
    setTag(consumer,0x61000037,0x70);
    appendTag(consumer,0x6100000f,0x50 | (uint64_t{0x100}<<32) | (uint64_t{2}<<48));
    auto multiple = batch(); multiple[0].file.request.dependencies.push_back("second.prx");
    multiple.push_back(input("second.prx",second,0x300000,{}));
    Linker::Report progress; Linker::Image multi;
    check(Linker::link(multiple,{},multi,diagnostic,4096,&progress),diagnostic.c_str());
    check(multi.modules.size()==3 && progress.dependencies==2 && progress.committed,"multiple PRX metadata dependencies");
    multiple.pop_back();
    check(!Linker::link(multiple,{},multi,diagnostic,4096,&progress) && !progress.committed && multi.modules.size()==3,"missing PRX retains previous batch");
    consumer=original;
    // DT_NEEDED uses only a checked SCE string-table offset, never filename guessing.
    const std::string needed="provider.prx";
    std::copy(needed.begin(),needed.end(),consumer.begin()+0x650); consumer[0x650+needed.size()]=0;
    setTag(consumer,0x61000037,0x70); appendTag(consumer,1,0x50);
    check(Linker::link(batch(),{},multi,diagnostic,4096,&progress) && progress.dependencies==2,"DT_NEEDED closed graph");
    auto noNeeded=batch(); noNeeded[0].file.request.dependencies.clear(); reject(noNeeded,"missing explicit dependency");
    appendTag(consumer,1,0x50); reject(batch(),"duplicate DT_NEEDED"); consumer=original;
    appendTag(consumer,1,UINT64_MAX); reject(batch(),"string offset"); consumer=original;
    // A compatible identity supplied twice is ambiguous even without any exports.
    auto noSymbols=provider; setTag(noSymbols,0x6100003f,24);
    auto ambiguous=batch(); ambiguous[0].file.request.dependencies.push_back("duplicate.prx");
    ambiguous.push_back(input("duplicate.prx",noSymbols,0x300000,{})); reject(ambiguous,"ambiguous PRX module");
    setTag(provider,0x6100000d,uint64_t(moduleOffset-0x600)|(uint64_t{0x200}<<32)|(uint64_t{1}<<48));
    reject(batch(),"incompatible"); provider=make(true);
    // Static executable preparation needs no invented dynamic tables/imports.
    // PT_SHLIB (5) has unspecified semantics and stays refused (PT_INTERP is accepted since mission 49).
    put(consumer,64+3*56,5,4); reject(batch(),"unsupported runtime program header 5"); consumer=original;
    // PT_SCE_MODULE_PARAM: exact 24-byte view of the RW PT_LOAD (mission 46).
    // Layout mirrors libc.prx/libSceFios2.prx; the view is never mapped itself.
    auto withParam = [&](std::vector<uint8_t> b, uint64_t address = 0x2010) {
        put(b,56,5,2); const size_t p = 64+4*56;
        put(b,p,0x61000002,4); put(b,p+4,4,4); put(b,p+8,0x300+address-0x2000); put(b,p+16,address);
        put(b,p+32,24); put(b,p+40,24); put(b,p+48,8);
        const size_t f = 0x300+address-0x2000;
        put(b,f,24); put(b,f+8,0x3C13F4BF,4); put(b,f+12,1,4); put(b,f+16,0x04008061,4); put(b,f+20,0,4);
        return b;
    };
    const auto paramProvider = withParam(make(true)), paramConsumer = withParam(original);
    provider = paramProvider; consumer = paramConsumer;
    Linker::Image params; Linker::Report paramReport;
    check(Linker::link(batch(),{},params,diagnostic,4096,&paramReport),diagnostic.c_str());
    check(paramReport.moduleParams==2 && paramReport.segments==4 && paramReport.memoryBytes==0x400 &&
        params.mappings().size()==4,"module param adds no mapping or memory");
    check(params.modules[0].moduleParam.present && params.modules[0].moduleParam.address==0x2010 &&
        params.modules[0].moduleParam.sdkVersion==0x04008061 && params.modules[1].moduleParam.version==1,"module param recorded");
    check(value(params,0x202010)==24 && value(params,0x102010)==24 && value(params,0x102000)==0x201000,
        "module param bytes come from PT_LOAD; ordinary relocation unaffected");
    check(paramReport.text().find("sans écriture) : 2")!=std::string::npos,"module param reported");
    auto rejectParam = [&](size_t offset, uint64_t v, size_t n, const char* expected) {
        consumer = paramConsumer; put(consumer,offset,v,n); reject(batch(),expected); consumer = paramConsumer;
    };
    const size_t ph = 64+4*56, field = 0x310;
    rejectParam(ph+4,6,4,"read-only flags");
    rejectParam(ph+32,32,8,"24-byte"); rejectParam(ph+40,32,8,"24-byte");
    rejectParam(ph+48,4,8,"alignment"); rejectParam(ph+16,0x2014,8,"alignment");
    rejectParam(ph+16,0x20f0,8,"outside loadable"); rejectParam(ph+16,0x3000,8,"outside loadable");
    rejectParam(ph+16,0x1010,8,"executable owner"); rejectParam(ph+8,0x318,8,"file offset differs");
    rejectParam(field,32,8,"size field"); rejectParam(field+8,0x3C13F4BE,4,"magic");
    rejectParam(field+12,2,4,"unsupported version"); rejectParam(field+20,1,4,"reserved field");
    // File-backed requirement: shrink PT_LOAD file bytes so the view lands in BSS.
    rejectParam(64+56+32,0x10,8,"file-backed");
    // A RELRO owner is not the verified profile.
    rejectParam(64+56,0x61000010,4,"PT_LOAD owner");
    // The verified layout has no pointer: any relocation into it is refused.
    rejectParam(0x900,0x2018,8,"relocation into PT_SCE_MODULE_PARAM");
    rejectParam(0x900,0x200c,8,"relocation into PT_SCE_MODULE_PARAM");
    consumer = paramConsumer; put(consumer,0x900,0x2028); // Adjacent (after) write is allowed.
    check(Linker::link(batch(),{},params,diagnostic),diagnostic.c_str()); check(value(params,0x102028)==0x201000,"adjacent relocation");
    consumer = paramConsumer; put(consumer,0x900,0x2008); // Adjacent (before) write is allowed.
    check(Linker::link(batch(),{},params,diagnostic),diagnostic.c_str()); check(value(params,0x102008)==0x201000,"adjacent relocation before");
    // Duplicate header, TLS overlap: refused before any write.
    consumer = paramConsumer; put(consumer,56,6,2); std::copy(consumer.begin()+ph,consumer.begin()+ph+56,consumer.begin()+ph+56);
    reject(batch(),"duplicate header");
    consumer = paramConsumer; put(consumer,56,6,2);
    { const size_t t=ph+56; put(consumer,t,7,4); put(consumer,t+4,4,4); put(consumer,t+8,0x300); put(consumer,t+16,0x2000);
      put(consumer,t+32,0); put(consumer,t+40,0x18); put(consumer,t+48,8); }
    reject(batch(),"TLS not supported");
    { std::string why;
      // TLS overlap is diagnosed by the module-param validator itself as well.
      maxps4::PS4ExecutableImage staged; check(!staged.load(consumer.data(),consumer.size()),"TLS fixture stages");
      maxps4::OrbisModuleParam::Value v; check(!maxps4::OrbisModuleParam::validate(staged,v,why) && why.find("overlaps PT_TLS")!=std::string::npos,why.c_str());
    }
    // Process param (PT_SCE_PROCPARAM) remains an explicit refusal.
    consumer = paramConsumer; put(consumer,ph,0x61000001,4); reject(batch(),"PT_SCE_PROCPARAM: 72-byte file-backed profile required");
    provider = make(true); consumer = original;
    // PT_GNU_EH_FRAME (mission 47): unwinding metadata viewed inside text PT_LOAD.
    auto withEH = [](std::vector<uint8_t> b) { eh_fixture::addEHFrame(b,4,0x1000,0x200); return b; };
    const auto ehProvider = withEH(make(true)), ehConsumer = withEH(original);
    provider = ehProvider; consumer = ehConsumer;
    Linker::Image eh; Linker::Report ehReport;
    check(Linker::link(batch(),{},eh,diagnostic,4096,&ehReport),diagnostic.c_str());
    check(ehReport.ehFrameHeaders==2 && ehReport.segments==4 && ehReport.memoryBytes==0x400 && eh.mappings().size()==4,
        "EH_FRAME adds no mapping or memory");
    { const auto& v = eh.modules[1].ehFrameHeader;
      check(v.present && v.address==0x10e0 && v.size==28 && v.ehFrame==0x10a0 && v.fdeCount==2 &&
          v.firstLocation==0x1000 && v.lastLocation==0x1010,"EH_FRAME recorded module-relative"); }
    check(eh.translate(0x1010e0,28,5)!=nullptr && value(eh,0x1010e0)==0x3b031b01ULL+(uint64_t(uint32_t(-0x44))<<32),
        "EH_FRAME bytes are read from the RX text mapping");
    check(!eh.translate(0x1010e0,1,2),"EH_FRAME never writable");
    check(ehReport.text().find("aucun unwinder) : 2")!=std::string::npos,"EH_FRAME reported");
    const size_t ehPh = 64+4*56, hdr = eh_fixture::headerFile(0x200);
    auto rejectEH = [&](size_t offset, uint64_t v, size_t n, const char* expected) {
        consumer = ehConsumer; put(consumer,offset,v,n); reject(batch(),expected); consumer = ehConsumer;
    };
    rejectEH(ehPh+4,5,4,"read-only flags"); rejectEH(ehPh+4,6,4,"read-only flags");
    rejectEH(ehPh+40,32,8,"at least 12 bytes"); rejectEH(ehPh+32,8,8,"at least 12 bytes");
    rejectEH(ehPh+48,8,8,"4-byte alignment"); rejectEH(ehPh+16,0x10e2,8,"4-byte alignment");
    rejectEH(ehPh+16,0x10f0,8,"outside loadable"); rejectEH(ehPh+16,0x3000,8,"outside loadable");
    rejectEH(ehPh+16,0x2000,8,"readable non-writable owner"); rejectEH(ehPh+8,0x2e4,8,"file offset differs");
    rejectEH(64+32,0xf0,8,"file-backed");
    rejectEH(hdr,2,1,"unsupported version"); rejectEH(hdr+1,0x03,1,"pointer encodings");
    rejectEH(hdr+2,0x0b,1,"pointer encodings"); rejectEH(hdr+3,0x1b,1,"pointer encodings");
    rejectEH(hdr+4,0,4,"eh_frame outside"); rejectEH(hdr+4,uint32_t(-0xf0),4,"eh_frame outside");
    rejectEH(hdr+8,0,4,"count out of bounds"); rejectEH(hdr+8,3,4,"count out of bounds");
    rejectEH(hdr+12,uint32_t(-0xd0),4,"strictly sorted");
    rejectEH(hdr+20,uint32_t(0x2000-0x10e0),4,"outside file-backed code");
    rejectEH(hdr+16,0,4,"FDE outside eh_frame"); rejectEH(hdr+16,uint32_t(-0x50),4,"FDE outside eh_frame");
    rejectEH(0x2b0,0,4,"FDE length"); rejectEH(0x2b0,0xffffffff,4,"FDE length"); rejectEH(0x2c0,0x40,4,"FDE length");
    rejectEH(0x2b4,0,4,"CIE pointer"); rejectEH(0x2b4,0x100,4,"CIE pointer"); rejectEH(0x2b4,8,4,"CIE pointer");
    rejectEH(0x2a4,1,4,"does not reference a CIE"); rejectEH(0x2a0,0,4,"CIE length");
    { // Trailing table bytes must be zero padding (observed on libc.prx).
      consumer = ehConsumer; put(consumer,ehPh+32,32); put(consumer,ehPh+40,32);
      check(Linker::link(batch(),{},eh,diagnostic),diagnostic.c_str());
      put(consumer,hdr+29,1,1); reject(batch(),"non-zero bytes after table"); consumer = ehConsumer; }
    { // Duplicate header and RELRO owner.
      consumer = ehConsumer; std::copy(consumer.begin()+ehPh,consumer.begin()+ehPh+56,consumer.begin()+ehPh+56);
      put(consumer,56,6,2); reject(batch(),"duplicate header"); consumer = ehConsumer;
      std::copy(consumer.begin()+0x2a0,consumer.begin()+0x2fc,consumer.begin()+0x3a0);
      put(consumer,64+56,0x61000010,4); put(consumer,64+56+4,4,4); put(consumer,ehPh+8,0x3e0); put(consumer,ehPh+16,0x20e0);
      reject(batch(),"PT_LOAD owner required"); consumer = ehConsumer; }
    // A relocation can never write into the read-only unwinding table.
    rejectEH(0x900,0x10e4,8,"relocation target outside RW/non-X");
    // 0x6fffff00/0x6fffff01: accepted only as inert entries, never read.
    consumer = ehConsumer;
    eh_fixture::program(consumer,5,0x6fffff00,0,0xb00,0,0x10,0,0x10);
    eh_fixture::program(consumer,6,0x6fffff01,0,0xb10,0,0x8,0,0x10);
    const auto opaqueConsumer = consumer;
    check(Linker::link(batch(),{},eh,diagnostic,4096,&ehReport),diagnostic.c_str());
    check(ehReport.opaqueHeaders==2 && ehReport.segments==4 && ehReport.memoryBytes==0x400,"opaque headers add nothing");
    for (const auto& field : {std::pair<size_t,uint64_t>{40,8},{16,0x1000},{4,4}}) {
        consumer = opaqueConsumer; put(consumer,64+5*56+field.first,field.second,field.first==4 ? 4 : 8);
        reject(batch(),"with memory footprint refused");
        consumer = opaqueConsumer; put(consumer,64+6*56+field.first,field.second,field.first==4 ? 4 : 8);
        reject(batch(),"with memory footprint refused");
    }
    // Other GNU/OS types are not implied by this acceptance.
    consumer = opaqueConsumer; put(consumer,64+5*56,0x6474e551,4); reject(batch(),"unsupported runtime program header 1685382481");
    consumer = opaqueConsumer; put(consumer,64+5*56,0x6fffff02,4); reject(batch(),"unsupported runtime program header");
    // Module param + EH_FRAME + opaque in one module: same real header set as Fios2.
    // withParam writes program header 4, so the EH_FRAME entry moves to index 7.
    auto combined = [&](const std::vector<uint8_t>& withEHFrame) {
        auto result = withParam(withEHFrame);
        std::copy(withEHFrame.begin()+ehPh,withEHFrame.begin()+ehPh+56,result.begin()+64+7*56);
        put(result,56,8,2); return result;
    };
    provider = combined(ehProvider); consumer = combined(opaqueConsumer);
    check(Linker::link(batch(),{},eh,diagnostic,4096,&ehReport),diagnostic.c_str());
    check(ehReport.moduleParams==2 && ehReport.ehFrameHeaders==2 && ehReport.opaqueHeaders==2 && eh.modules[1].moduleParam.present &&
        eh.modules[1].ehFrameHeader.present && ehReport.memoryBytes==0x400,"param, EH_FRAME and opaque headers together");
    provider = make(true); consumer = original;
    // Mission 48: dynamic tags observed on CUSA06015, validated as metadata only.
    auto retag = [](std::vector<uint8_t>& b, uint64_t from, uint64_t to) {
        for (size_t p=0x400;p<0x600;p+=16) { uint64_t t=0; for (size_t i=0;i<8;++i) t|=uint64_t(b[p+i])<<(8*i);
            if (t==from) { put(b,p,to); return; } }
        throw std::runtime_error("retag");
    };
    auto removeTag = [](std::vector<uint8_t>& b, uint64_t tag) { // Shift later entries; keep DT_NULL last.
        for (size_t p=0x400;p<0x600;p+=16) { uint64_t t=0; for (size_t i=0;i<8;++i) t|=uint64_t(b[p+i])<<(8*i);
            if (t==tag) { std::copy(b.begin()+p+16,b.begin()+0x600,b.begin()+p); std::fill(b.begin()+0x5f0,b.begin()+0x600,0); return; } }
        throw std::runtime_error("removeTag");
    };
    auto dynProvider = make(true), dynConsumer = original;
    dyn_fixture::add(dynProvider,{"provider.prx",0x1000,0x2100,0,1,0,false});
    dyn_fixture::add(dynConsumer,{"",0x1000,0x2100,0x2040,0,1,false});
    provider = dynProvider; consumer = dynConsumer;
    Linker::Image dynImage; Linker::Report dynReport;
    check(Linker::link(batch(),{},dynImage,diagnostic,4096,&dynReport),diagnostic.c_str());
    check(dynReport.recognisedDynamicTags==34 && dynReport.initFunctions==2 && dynReport.finiFunctions==2 &&
        dynReport.initArrayEntries==0 && dynImage.mappings().size()==4 && dynReport.memoryBytes==0x400,"dynamic tags add no memory");
    { const auto& pi = dynImage.modules[0].dynamic; const auto& ci = dynImage.modules[1].dynamic;
      check(dynImage.modules[0].path=="provider.prx" && pi.hash && pi.buckets==1 && pi.chains==2 && pi.fingerprint &&
          pi.fingerprintBytes[0]==0xa0 && pi.fingerprintBytes[19]==0xb3 && pi.soname=="provider.prx" &&
          pi.originalFilename=="W:/Build/fixture path/provider.prx" && pi.init && pi.initAddress==0x1000 && pi.fini &&
          pi.initArray.present && !pi.initArray.size && pi.exportLibraryAttributes.at(1)==1 && !pi.textrel,"provider dynamic info");
      check(ci.soname.empty() && ci.pltgot && ci.pltgotAddress==0x2040 && ci.importLibraryAttributes.at(1)==9,"consumer dynamic info"); }
    check(dynReport.text().find("exécutés : 0")!=std::string::npos,"init/fini never executed");
    auto rejectDyn = [&](auto mutate, const char* expected) {
        provider = dynProvider; consumer = dynConsumer; mutate(); reject(batch(),expected); provider = dynProvider; consumer = dynConsumer;
    };
    const size_t h = dyn_fixture::hashFile;
    // SCE hash table: structure only, never used for symbol lookup.
    rejectDyn([&]{ put(consumer,h+4,3,4); },"hash chain count differs");
    rejectDyn([&]{ put(consumer,h,2,4); },"hash table size differs");
    rejectDyn([&]{ put(consumer,h+8,2,4); },"hash index out of bounds");
    rejectDyn([&]{ put(consumer,h+16,1,4); },"hash chain cycle"); // chain[1]=1 loops forever.
    rejectDyn([&]{ put(consumer,h+16,2,4); },"hash index out of bounds");
    rejectDyn([&]{ put(consumer,h+8,0,4); },"cover every symbol");
    rejectDyn([&]{ put(consumer,h+12,1,4); },"chain[0]");
    rejectDyn([&]{ setTag(consumer,0x6100003d,18); },"hash table size");
    rejectDyn([&]{ setTag(consumer,0x61000025,0x542); },"DT_SCE_HASH alignment");
    rejectDyn([&]{ setTag(consumer,0x61000025,0x5f8); },"SCE table out of bounds");
    rejectDyn([&]{ setTag(consumer,0x6100003d,uint64_t{1}<<40); },"DT_SCE_HASH alignment or budget");
    rejectDyn([&]{ retag(consumer,0x61000025,0x6100003b+0x100000); },"unsupported dynamic tag");
    rejectDyn([&]{ removeTag(consumer,0x61000025); },"pair incomplete");
    rejectDyn([&]{ removeTag(consumer,0x6100003d); },"pair incomplete");
    // Fingerprint, names and attributes.
    rejectDyn([&]{ setTag(consumer,0x61000007,0x5f0); },"SCE table out of bounds");
    rejectDyn([&]{ setTag(provider,14,dyn_fixture::appendString(provider,"other.prx")); },"DT_SONAME differs");
    rejectDyn([&]{ setTag(provider,0x61000009,0xffff); },"string offset");
    rejectDyn([&]{ provider[0x600+dyn_fixture::getTag(provider,0x61000009)+3]=0x07; },"invalid original filename");
    rejectDyn([&]{ setTag(consumer,0x61000011,1); },"DT_SCE_MODULE_ATTR");
    rejectDyn([&]{ setTag(consumer,21,1); },"DT_DEBUG");
    rejectDyn([&]{ setTag(consumer,30,1); },"DT_FLAGS bits"); rejectDyn([&]{ setTag(consumer,30,8); },"DT_FLAGS bits");
    rejectDyn([&]{ setTag(provider,0x61000017,(uint64_t{1}<<48)|9); },"library attribute value");
    rejectDyn([&]{ setTag(consumer,0x61000019,(uint64_t{1}<<48)|1); },"library attribute value");
    rejectDyn([&]{ setTag(consumer,0x61000019,(uint64_t{7}<<48)|9); },"without library descriptor");
    rejectDyn([&]{ orbis_fixture::appendTag(consumer,0x61000019,(uint64_t{1}<<48)|9); },"duplicate library attribute");
    rejectDyn([&]{ orbis_fixture::appendTag(consumer,21,0); },"duplicate dynamic tag");
    // Init/fini functions and arrays: bounds, alignment, permissions.
    rejectDyn([&]{ setTag(consumer,12,0x2000); },"DT_INIT outside file-backed code");
    rejectDyn([&]{ setTag(consumer,13,0x1100); },"DT_FINI outside file-backed code");
    rejectDyn([&]{ setTag(consumer,25,0x1000); },"DT_INIT_ARRAY outside");
    rejectDyn([&]{ setTag(consumer,26,0x2004); },"DT_FINI_ARRAY outside");
    rejectDyn([&]{ setTag(consumer,33,4); },"DT_PREINIT_ARRAY outside");
    rejectDyn([&]{ setTag(consumer,27,8); },"DT_INIT_ARRAY outside"); // 0x2100 + 8 beyond file bytes.
    rejectDyn([&]{ setTag(consumer,25,0x2000); setTag(consumer,27,128*1024); },"DT_INIT_ARRAY outside");
    rejectDyn([&]{ removeTag(consumer,28); },"pair incomplete");
    rejectDyn([&]{ removeTag(consumer,32); },"pair incomplete");
    // PLTGOT: placement and reserved entries never written by JMPREL.
    rejectDyn([&]{ setTag(consumer,0x61000027,0x2044); },"DT_SCE_PLTGOT outside");
    rejectDyn([&]{ setTag(consumer,0x61000027,0x1000); },"DT_SCE_PLTGOT outside");
    rejectDyn([&]{ retag(consumer,0x6100002f,0x61000029); retag(consumer,0x61000031,0x6100002d); retag(consumer,0x61000033,0x6100002b);
        setTag(consumer,0x6100002b,7); setTag(consumer,0x61000027,0x1ff0); },"DT_SCE_PLTGOT outside"); // RW starts at 0x2000.
    rejectDyn([&]{ retag(consumer,0x6100002f,0x61000029); retag(consumer,0x61000031,0x6100002d); retag(consumer,0x61000033,0x6100002b);
        setTag(consumer,0x6100002b,7); setTag(consumer,0x61000027,0x2000); },"JMPREL writes reserved PLTGOT entries");
    { provider = dynProvider; consumer = dynConsumer; // JMPREL beyond the 3 reserved slots is accepted.
      retag(consumer,0x6100002f,0x61000029); retag(consumer,0x61000031,0x6100002d); retag(consumer,0x61000033,0x6100002b);
      setTag(consumer,0x6100002b,7); put(consumer,0x900,0x2058); setTag(consumer,0x61000027,0x2040);
      check(Linker::link(batch(),{},dynImage,diagnostic),diagnostic.c_str()); check(value(dynImage,0x102058)==0x201000,"JMPREL after PLTGOT"); }
    // Init array entry: must be a pointer relocation landing in module code.
    { provider = dynProvider; consumer = dynConsumer;
      setTag(consumer,25,0x2050); setTag(consumer,27,8); setTag(consumer,0x61000031,48);
      put(consumer,0x918,0x2050); put(consumer,0x920,8); put(consumer,0x928,0x1000);
      const auto withEntry = consumer;
      check(Linker::link(batch(),{},dynImage,diagnostic,4096,&dynReport),diagnostic.c_str());
      check(dynReport.initArrayEntries==1 && value(dynImage,0x102050)==0x101000,"init array entry relocated to code (not executed)");
      consumer = withEntry; put(consumer,0x928,0x2000); reject(batch(),"init/fini array entry outside module code");
      consumer = withEntry; put(consumer,0x928,0x1100); reject(batch(),"init/fini array entry outside module code");
      consumer = withEntry; setTag(consumer,0x61000031,24); reject(batch(),"without pointer relocation");
      consumer = withEntry; put(consumer,0x918,0x2058); reject(batch(),"without pointer relocation");
      consumer = withEntry; setTag(consumer,32,0x2050); setTag(consumer,33,8); setTag(consumer,26,0x2050); setTag(consumer,28,8);
      check(Linker::link(batch(),{},dynImage,diagnostic,4096,&dynReport) && dynReport.initArrayEntries==3,"shared entry counted per array"); }
    // DT_TEXTREL / DF_TEXTREL: accepted only while no relocation targets code.
    { auto textrel = original; dyn_fixture::add(textrel,{"",0x1000,0x2100,0,0,0,true});
      provider = dynProvider; consumer = textrel;
      check(Linker::link(batch(),{},dynImage,diagnostic),diagnostic.c_str());
      check(dynImage.modules[1].dynamic.textrel && !dynImage.translate(0x101000,1,2) && value(dynImage,0x102000)==0x201000,
          "TEXTREL module stays W^X with data relocations");
      put(consumer,0x900,0x1008); reject(batch(),"DT_TEXTREL relocation into executable segment refused (W^X)");
      consumer = textrel; setTag(consumer,22,1); reject(batch(),"DT_TEXTREL value"); }
    // Tags without established behaviour stay refused.
    for (uint64_t tag : {16U, 24U, 29U, 0x6100001bU, 0x61000005U}) {
        provider = dynProvider; consumer = dynConsumer; orbis_fixture::appendTag(consumer,tag,0);
        reject(batch(),"unsupported dynamic tag");
    }
    // Dependency order is unchanged: the provider (declared dependency) stays first.
    provider = dynProvider; consumer = dynConsumer;
    { auto reversed = batch(); std::reverse(reversed.begin(),reversed.end());
      check(Linker::link(reversed,{},dynImage,diagnostic) && dynImage.modules[0].path=="provider.prx" &&
          dynImage.modules[0].dynamic.init,"dependency-first order with dynamic info"); }
    provider = make(true); consumer = original;
    // Mission 49: undefined plain-name symbols are never given an address.
    using mission49_fixture::addSymbol;
    auto symConsumer = original;
    const auto weakIdx = addSymbol(symConsumer,dyn_fixture::appendString(symConsumer,"module_start"),0x20,0,0,0,0);
    const auto globalIdx = addSymbol(symConsumer,dyn_fixture::appendString(symConsumer,"__start__Ztext"),0x10,0,0,0,0);
    provider = make(true); consumer = symConsumer;
    Linker::Image symImage; Linker::Report symReport;
    check(Linker::link(batch(),{},symImage,diagnostic,4096,&symReport),diagnostic.c_str());
    check(symReport.unboundWeakSymbols==1 && symReport.unboundGlobalSymbols==1 && symReport.imports==1 &&
        value(symImage,0x102000)==0x201000,"unreferenced weak/global plain names accepted, never addressed");
    auto rejectSym = [&](auto mutate, const char* expected) { consumer = symConsumer; mutate(); reject(batch(),expected); consumer = symConsumer; };
    rejectSym([&]{ put(consumer,0x908,(uint64_t(weakIdx)<<32)|7); },"unbound weak undefined symbol module_start");
    rejectSym([&]{ put(consumer,0x908,(uint64_t(globalIdx)<<32)|1); },"unbound global undefined symbol __start__Ztext");
    rejectSym([&]{ put(consumer,0x908,(uint64_t(weakIdx)<<32)|6); },"unbound weak");
    const size_t weakSym = 0x800+weakIdx*24, globalSym = 0x800+globalIdx*24;
    rejectSym([&]{ consumer[weakSym+4]=0x22; },"invalid NID identity: module_start");   // weak FUNC: not the NOTYPE profile.
    rejectSym([&]{ put(consumer,weakSym+16,8); },"invalid NID identity: module_start");  // non-zero size.
    rejectSym([&]{ consumer[weakSym+4]=0x00; },"invalid undefined symbol: module_start"); // local undefined.
    rejectSym([&]{ put(consumer,globalSym+8,0x10); },"invalid undefined symbol: __start__Ztext");
    rejectSym([&]{ consumer[weakSym+5]=3; },"visibility/section: module_start");
    rejectSym([&]{ std::string bad="mod-start"; std::copy(bad.begin(),bad.end(),consumer.begin()+0x600+dyn_fixture::getTag(consumer,0x61000037)-28); },
        "invalid NID identity");
    // STV_PROTECTED: a defined export binds locally and is never preempted.
    provider = make(true); consumer = original; provider[0x81d]=3;
    check(Linker::link(batch(),{},symImage,diagnostic,4096,&symReport) && symReport.protectedSymbols==1 &&
        value(symImage,0x102000)==0x201000,"protected export resolves exactly");
    provider[0x81d]=2; reject(batch(),"visibility"); provider[0x81d]=1; reject(batch(),"visibility");
    provider = make(true); consumer[0x81d]=3; reject(batch(),"visibility"); consumer = original; // undefined protected.
    consumer[0x81c]=0x16; reject(batch(),"STT_TLS symbol requires the TLS linking path"); consumer = original;
    provider[0x81c]=0x16; reject(batch(),"STT_TLS symbol requires the TLS linking path"); provider = make(true);
    // PT_SCE_PROCPARAM / PT_INTERP views in the executable (mission 49).
    auto procConsumer = original;
    eh_fixture::program(procConsumer,4,0x61000001,4,0x310,0x2010,72,72,8);
    put(procConsumer,0x310,72); put(procConsumer,0x318,0x4942524f,4); put(procConsumer,0x31c,2,4); put(procConsumer,0x320,0x04008061,4);
    put(procConsumer,0x348,0x2080); put(procConsumer,0x350,0x2088);
    setTag(procConsumer,0x61000031,72);
    put(procConsumer,0x918,0x2048); put(procConsumer,0x920,8); put(procConsumer,0x928,0x2080);
    put(procConsumer,0x930,0x2050); put(procConsumer,0x938,8); put(procConsumer,0x940,0x2088);
    eh_fixture::program(procConsumer,5,3,4,0x220,0x1020,21,21,1);
    { const std::string interp="/libexec/ld-elf.so.1"; std::copy(interp.begin(),interp.end(),procConsumer.begin()+0x220); procConsumer[0x234]=0; }
    provider = make(true); consumer = procConsumer;
    check(Linker::link(batch(),{},symImage,diagnostic,4096,&symReport),diagnostic.c_str());
    check(symReport.processParams==1 && symReport.interpreters==1 && symImage.modules[1].processParam.present &&
        symImage.modules[1].processParam.address==0x2010 && symImage.modules[1].processParam.sdkVersion==0x04008061 &&
        symImage.modules[1].processParam.interpreter=="/libexec/ld-elf.so.1" && symImage.mappings().size()==4 && symReport.memoryBytes==0x400,
        "PROCPARAM/INTERP are unmapped views");
    check(value(symImage,0x102048)==0x102080 && value(symImage,0x102050)==0x102088 && value(symImage,0x102000)==0x201000,"PROCPARAM pointers relocated");
    auto rejectProc = [&](size_t offset, uint64_t v, size_t n, const char* expected) {
        consumer = procConsumer; put(consumer,offset,v,n); reject(batch(),expected); consumer = procConsumer;
    };
    const size_t pp = 64+4*56, ip = 64+5*56;
    rejectProc(0x318,0x4942524e,4,"PROCPARAM: magic"); rejectProc(0x31c,3,4,"unsupported version");
    rejectProc(0x310,80,8,"size field"); rejectProc(0x330,1,1,"reserved bytes");
    rejectProc(pp+4,6,4,"read-only flags"); rejectProc(pp+48,4,8,"8-byte alignment"); rejectProc(pp+32,64,8,"72-byte");
    rejectProc(pp+8,0x318,8,"not a file-backed view"); rejectProc(pp+16,0x20d0,8,"not a file-backed view");
    { consumer = procConsumer; put(consumer,pp+8,0x260); put(consumer,pp+16,0x1060); reject(batch(),"executable owner refused"); consumer = procConsumer; }
    rejectProc(0x928,0x1000,8,"pointer outside file-backed module data");
    rejectProc(0x918,0x2018,8,"write outside its RELATIVE pointer fields");
    rejectProc(0x920,(uint64_t{1}<<32)|1,8,"write outside its RELATIVE pointer fields");
    { consumer = procConsumer; setTag(consumer,0x61000031,48); reject(batch(),"pointer field without RELATIVE relocation"); consumer = procConsumer; }
    rejectProc(0x220,'l',1,"absolute NUL-terminated"); rejectProc(0x234,'x',1,"absolute NUL-terminated");
    rejectProc(0x225,0x07,1,"printable path"); rejectProc(ip+4,5,4,"read-only 2..256"); rejectProc(ip+32,300,8,"read-only 2..256");
    rejectProc(ip+16,0x10f0,8,"not a file-backed view");
    { consumer = procConsumer; std::copy(consumer.begin()+pp,consumer.begin()+pp+56,consumer.begin()+64+6*56); put(consumer,56,7,2);
      reject(batch(),"PT_SCE_PROCPARAM: duplicate header"); consumer = procConsumer; }
    { auto prx = make(true); eh_fixture::program(prx,4,3,4,0x220,0x1020,21,21,1); provider = prx;
      reject(batch(),"executable module required"); provider = make(true); }
    provider = make(true); consumer = original;
    auto standalone=makeStatic(); Linker::Image staticImage;
    check(Linker::link({input("static.elf",standalone,0x400000,{})},{},staticImage,diagnostic,4096,&progress),diagnostic.c_str());
    check(progress.committed && progress.imports==0 && progress.relocations==0 && progress.memoryBytes==0x300,"static report counts");
    check(staticImage.translate(0x402100,0x100,6) && value(staticImage,0x402100)==0,"BSS is zero and writable");
    check(!staticImage.translate(0x401000,1,2) && !staticImage.translate(0x402000,1,1),"guest W/X permissions");
    check(!staticImage.translate(UINT64_MAX,8,4) && !staticImage.translate(0x4021ff,2,4),"exact memory boundaries");
    auto snapshot=staticImage.mappings(); const auto* owned=snapshot[0].data;
    auto moved=std::move(staticImage); check(moved.translate(snapshot[0].address,1,4)==owned,"mapping ownership follows moved image");
    // Invalid later module/relocation yields no committed counters or writes.
    setTag(consumer,0x61000031,48); put(consumer,0x918,0x2008); put(consumer,0x920,(uint64_t{1}<<32)|99);
    check(!Linker::link(batch(),{},moved,diagnostic,4096,&progress),"late failure with report");
    check(progress.phase=="relocations" && progress.validatedRelocations==1 && !progress.committed &&
        progress.text().find("appliquées : 0")!=std::string::npos && moved.modules.size()==1,"failure report is not a positive result"); consumer=original;
    auto nonCanonical=batch(); nonCanonical[1].bias=uint64_t{1}<<47; reject(nonCanonical,"non-canonical");
    auto edgeCanonical=batch(); edgeCanonical[1].bias=(uint64_t{1}<<47)-0x2000; reject(edgeCanonical,"non-canonical");
    // Every truncation before the last required metadata byte must fail safely.
    for (size_t n=0;n<original.size();++n) {
        auto shortBatch=batch(); shortBatch[0].file.size=n;
        check(!Linker::link(shortBatch,{},moved,diagnostic,4096,&progress) && !progress.committed,"truncated file rejected transactionally");
    }
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
    char detailed[4096];
    check(maxps4_orbis_link_with_report(native,2,1024,&handle,detailed,sizeof(detailed))==0,"native detailed preparation");
    check(std::string(detailed).find("Préparation : réussie")!=std::string::npos &&
        std::string(detailed).find("Exécution : impossible")!=std::string::npos,"preparation distinct from execution");
    maxps4_orbis_close(handle); handle=nullptr;
    check(maxps4_orbis_link_with_report(native,2,1024,&handle,detailed,8)==-ENOSPC && !handle,"truncated report cannot commit image");
    native[0].size=63;
    check(maxps4_orbis_link_with_report(native,2,1024,&handle,detailed,sizeof(detailed))<0 && !handle &&
        std::string(detailed).find("image non engagée")!=std::string::npos,"native rejected report");
    native[0].size=1;
    check(maxps4_orbis_link(native,2,1024,&handle,report,sizeof(report))<0 && !handle && report[0],"failed native transaction");
    std::cout << "Orbis linker: " << checks << " checks passed\n";
}
