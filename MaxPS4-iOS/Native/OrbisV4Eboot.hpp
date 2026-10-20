#pragma once
#include "OrbisV4PRXLaunch.hpp"
#include <sstream>

namespace maxps4 {
// A separate plaintext file, never a PKG/PFS extraction path. No guessed ABI
// for arbitrary commercial code: opt in to the existing finite function ABI.
struct OrbisV4Eboot {
    static constexpr size_t file_limit=32*1024*1024;
    static int request(const uint8_t* bytes,size_t size,maxps4_v4_prx_request& output,
                       std::string& report) {
        output={};
        report="EBOOT: separate file; no PKG/PFS access; CPU_executed=false\n";
        if (!bytes || size<64) { report+="EBOOT.INPUT: missing/truncated file\n"; return -EINVAL; }
        if (size>file_limit) { report+="EBOOT.INPUT: file exceeds 32 MiB\n"; return -EFBIG; }
        PS4ExecutableImage image;
        const int status=image.load(bytes,size,PS4ExecutableImage::SelfPolicy::StageSignedClear);
        const bool self=OrbisMetadata::read(bytes,4)==0x1d3d154f;
        report+=self ? "format=SELF\n" : (!std::memcmp(bytes,"\177ELF",4) ? "format=ELF\n" :
            (!std::memcmp(bytes,"\177CNT",4) ? "format=PKG/CNT; separate plaintext executable required\n" : "format=unsupported\n"));
        if (status) {
            if (self) {
                const uint64_t declared=OrbisMetadata::read(bytes+16,4);
                report+="SELF declared_file_bytes="+std::to_string(declared)+
                    " actual_file_bytes="+std::to_string(size)+"\n";
                if (std::string(image.stage)=="SELF.HEADER" &&
                    (declared>size || OrbisMetadata::read(bytes+12,2)+OrbisMetadata::read(bytes+14,2)>declared))
                    report+="EBOOT.SELF_SIZE: declared extent/header exceeds available bounds\n";
            }
            report+="EBOOT.IMAGE: "+std::string(image.stage)+" index="+
                std::to_string(image.diagnosticIndex)+" status="+std::to_string(status)+"\n";
            if (self && (std::string(image.stage)=="SELF.BLOCK" ||
                std::string(image.stage)=="SELF.ENCRYPTED" ||
                std::string(image.stage)=="SELF.COMPRESSED_UNSUPPORTED")) {
                const size_t offset=32+image.diagnosticIndex*32;
                if (offset<=size && size-offset>=32) {
                    const uint64_t flags=OrbisMetadata::read(bytes+offset,8);
                    if (status==-ENOEXEC) report+="SELF block bounds/overlap malformed\n";
                    else if (flags&2U) report+="encrypted SELF block refused; no decryption\n";
                    else if (flags&8U) report+="compressed SELF block unsupported\n";
                    else if (flags&4U) report+="signed-clear SELF profile/flags unsupported; signature is not encryption and is not verified\n";
                    else report+="SELF block bounds/protected/unknown flags unsupported\n";
                }
            }
            if (self && std::string(image.stage)=="SELF.ASSOCIATION")
                report+="SELF digest/data descriptor association, alignment or size invalid\n";
            return status;
        }
        std::ostringstream details;
        details<<"image_staged=true (temporary data only); type=0x"<<std::hex<<image.type
               <<" entry=0x"<<image.entry<<"\n";
        for (const auto& segment:image.segments) {
            const auto& h=segment.header;
            details<<"segment guest=0x"<<h.address<<" permissions="<<h.flags<<std::dec
                   <<" file_bytes="<<h.fileSize<<" memory_bytes="<<h.memorySize<<std::hex<<"\n";
        }
        report+=details.str();
        if (self) {
            report+="signed_self_unverified="+std::to_string(image.signedSelfUnverified)+
                "; signature_verified=false; staging_only=true; guest_mapped=false\n";
            if (image.signedSelfUnverified) {
                report+="EBOOT.SELF_TRUST: signed content remains unauthenticated; inspection only, no V4 mapping/dispatch\n";
                return -ENOTSUP;
            }
            std::vector<uint8_t> canonical; std::string reason;
            const int adapted=OrbisV4SELF::adapt(bytes,size,canonical,reason);
            report+=reason+"\n";
            if (adapted) return adapted;
            output={bytes,size,0,image.entry,MAXPS4_V4_ABI_STARTUP_RETURN,64};
            report+="EBOOT.SELF: static unsigned candidate; dispatch revalidates through existing linker/V4\n";
            return 0;
        }
        OrbisMetadata metadata;
        if (!metadata.parse(bytes,size)) { report+="EBOOT.METADATA: "+metadata.error+"\n"; return -ENOEXEC; }
        report+="metadata_inventory_begin (analyzer only; not the V4 admission verdict)\n";
        report+=metadata.report();
        report+="metadata_inventory_end; separate V4 admission follows\n";
        if (image.type!=0xfe00 || image.requiresLinker || image.requiresTLS) {
            report+="EBOOT.ABI: only standalone ET_SCE_EXEC without dynamic linkage/PT_TLS; imports require an explicit supported runtime contract\n";
            return -ENOTSUP;
        }
        output={bytes,size,0,image.entry,MAXPS4_V4_ABI_STARTUP_RETURN,64};
        report+="EBOOT.ABI: experimental V4 function entry, RDI=argc/argv; no PS4 CRT\n";
        return 0;
    }
    static int preflight(const uint8_t* bytes,size_t size,std::string& report) {
        maxps4_v4_prx_request r{};
        int status=request(bytes,size,r,report);
        if (status) return status;
        std::unique_ptr<OrbisV4Translation> owner;
        std::string reason;
        status=OrbisV4PRXLaunch::prepare(r,[]{},owner,&reason);
        report+=reason+"\n";
        if (owner) { owner->close(); report+="guest_mapping_verified=true; mappings_retired=true; CPU_executed=false\n"; }
        return status;
    }
};
}
