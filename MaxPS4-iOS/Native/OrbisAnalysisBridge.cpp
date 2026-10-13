#include "OrbisAnalysisBridge.h"
#include "OrbisMetadata.hpp"
#include "maxps4_fex_runtime.h"
#include <cstdio>
#include <cerrno>
#include <memory>
#include <sys/stat.h>

int maxps4_orbis_analysis_report(const char* path, char* output, size_t capacity) {
    if (!output || !capacity) return -EINVAL;
    output[0] = 0;
    if (!path) return -EINVAL;
    auto copy = [&](const std::string& report, int status) {
        size_t count = std::min(report.size(),capacity-1);
        std::memcpy(output,report.data(),count); output[count] = 0;
        return count == report.size() ? status : -ENOSPC;
    };
    try {
        auto closeFile = [](FILE* f) { std::fclose(f); };
        std::unique_ptr<FILE,decltype(closeFile)> file(std::fopen(path,"rb"),closeFile);
        if (!file) return copy("Ouverture du fichier Orbis impossible",-errno);
        struct stat info{};
        if (fstat(fileno(file.get()),&info) || !S_ISREG(info.st_mode) || info.st_size < 64 ||
            uint64_t(info.st_size) > maxps4::OrbisMetadata::fileLimit)
            return copy("Rejet : fichier régulier de 64 octets à 64 Mio requis",-EINVAL);
        std::vector<uint8_t> bytes(size_t(info.st_size));
        if (std::fread(bytes.data(),1,bytes.size(),file.get()) != bytes.size())
            return copy("Lecture du fichier Orbis incomplète",-EIO);
        maxps4::OrbisMetadata metadata;
        bool valid = metadata.parse(bytes.data(),bytes.size(),[](const auto& s) {
            // MODULE_INFO packs minor then major; only libkernel major=1/minor=0
            // is admitted. A known NID in a different namespace stays unresolved.
            return s.moduleVersion == 0x0100 &&
                maxps4_ps4_resolve_libkernel(s.nid.c_str(),s.library.c_str(),s.module.c_str(),s.libraryVersion) != 0;
        });
        return copy(metadata.report(),valid ? 0 : -ENOEXEC);
    } catch (const std::bad_alloc&) { return copy("Allocation du diagnostic impossible",-ENOMEM); }
      catch (const std::exception&) { return copy("Erreur interne du diagnostic Orbis",-EIO); }
}
