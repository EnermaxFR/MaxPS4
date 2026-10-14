#include "OrbisAnalysisBridge.h"
#include "OrbisMetadata.hpp"
#include "OrbisLinker.hpp"
#include "GameDirectoryPreflight.hpp"
#include "maxps4_fex_runtime.h"
#include <cstdio>
#include <cerrno>
#include <memory>
#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>

int maxps4_game_directory_report(const char* path, char* output, size_t capacity) {
    if (!output || !capacity) return -EINVAL;
    output[0] = 0;
    std::string report;
    const int status = maxps4::GameDirectoryPreflight::report(path,report);
    const auto count = std::min(report.size(),capacity-1);
    std::memcpy(output,report.data(),count); output[count] = 0;
    return count == report.size() ? status : -ENOSPC;
}

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
        const int fd = ::open(path, O_RDONLY | O_CLOEXEC | O_NOFOLLOW | O_NONBLOCK);
        if (fd < 0) return copy("Ouverture du fichier Orbis impossible",-errno);
        struct Close { int fd; ~Close() { ::close(fd); } } close{fd};
        struct stat info{};
        if (fstat(fd,&info) || !S_ISREG(info.st_mode) || info.st_size < 64 ||
            uint64_t(info.st_size) > maxps4::OrbisMetadata::fileLimit)
            return copy("Rejet : fichier régulier de 64 octets à 64 Mio requis",-EINVAL);
        std::vector<uint8_t> bytes(size_t(info.st_size));
        size_t done = 0;
        while (done < bytes.size()) {
            const auto n = ::pread(fd, bytes.data()+done, bytes.size()-done, off_t(done));
            if (n < 0 && errno == EINTR) continue;
            if (n <= 0) return copy("Lecture du fichier Orbis incomplète",-EIO);
            done += size_t(n);
        }
        struct stat after{};
#if defined(__APPLE__)
        const auto modified = info.st_mtimespec, changed = info.st_ctimespec;
#else
        const auto modified = info.st_mtim, changed = info.st_ctim;
#endif
        if (fstat(fd,&after)) return copy("Vérification de la source Orbis impossible",-EIO);
#if defined(__APPLE__)
        const auto afterModified = after.st_mtimespec, afterChanged = after.st_ctimespec;
#else
        const auto afterModified = after.st_mtim, afterChanged = after.st_ctim;
#endif
        if (info.st_size != after.st_size || modified.tv_sec != afterModified.tv_sec ||
            modified.tv_nsec != afterModified.tv_nsec || changed.tv_sec != afterChanged.tv_sec ||
            changed.tv_nsec != afterChanged.tv_nsec)
            return copy("Source Orbis modifiée pendant la lecture",-EIO);
        return maxps4_orbis_bytes_report(bytes.data(), bytes.size(), output, capacity);
    } catch (const std::bad_alloc&) { return copy("Allocation du diagnostic impossible",-ENOMEM); }
      catch (const std::exception&) { return copy("Erreur interne du diagnostic Orbis",-EIO); }
}


int maxps4_orbis_bytes_report(const unsigned char* bytes, size_t size, char* output, size_t capacity) {
    if (!output || !capacity) return -EINVAL;
    output[0] = 0;
    if (!bytes || size < 64 || size > maxps4::OrbisMetadata::fileLimit) return -EINVAL;
    auto copy = [&](const std::string& report, int status) {
        const auto count = std::min(report.size(),capacity-1);
        std::memcpy(output,report.data(),count); output[count] = 0;
        return count == report.size() ? status : -ENOSPC;
    };
    try {
        maxps4::OrbisMetadata metadata;
        bool valid = metadata.parse(bytes,size,[](const auto& s) {
            // MODULE_INFO packs minor then major; only libkernel major=1/minor=0
            // is admitted. A known NID in a different namespace stays unresolved.
            return s.moduleVersion == 0x0100 &&
                maxps4_ps4_resolve_libkernel(s.nid.c_str(),s.library.c_str(),s.module.c_str(),s.libraryVersion) != 0;
        });
        // Inventory/HLE recognition is not a link binding. Prepare with an empty
        // registry and no external files; missing PRX/NID remains a real refusal.
        maxps4::OrbisLinker::Image image;
        maxps4::OrbisLinker::Report preparation;
        std::string diagnostic;
        maxps4::OrbisLinker::Input input{{{"selected.elf", {}}, bytes,size}, 0};
        maxps4::OrbisLinker::link({input}, {}, image, diagnostic,
            maxps4::PS4PKGRuntimePlan::batchMemoryBudget, &preparation);
        return copy(metadata.report() + "\nLes NID HLE reconnus ci-dessus ne constituent pas des bindings du runtime.\n\n" + preparation.text() + "\nImage de diagnostic temporaire ; aucune image conservée par l’application.", valid ? 0 : -ENOEXEC);
    } catch (const std::bad_alloc&) { return copy("Allocation du diagnostic impossible",-ENOMEM); }
      catch (const std::exception&) { return copy("Erreur interne du diagnostic Orbis",-EIO); }
}
