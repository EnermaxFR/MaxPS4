#pragma once
// Independently implemented subset of LibOrbisPkg PfsStructs/PfsReader/PFSBuilder
// at 643477263b2644e0803e0f58b8726ea4e3f3b7d4. No cryptography or upstream code.
#include "PS4PKGInventory.hpp"
#include <algorithm>
#include <map>
#include <set>
#include <stdexcept>

namespace maxps4 {
class PS4PFSReadOnly {
public:
    enum class Status { absent, encrypted, unknown, malformed, unsupported, accessible };
    struct File { std::string path; uint64_t offset, size; };
    Status status = Status::absent;
    std::string diagnostic = "PFS absent";
    uint16_t mode = 0;
    uint32_t blockSize = 0;
    std::vector<File> files;
    // Reader offsets are relative to this PFS slice, never to the entire CNT.
    bool inspect(uint64_t size, const PS4PKGInventory::Reader& read) {
        *this = PS4PFSReadOnly{};
        if (!size) return false;
        try {
            imageSize = size;
            std::array<uint8_t,0x48> h{};
            if (size < h.size() || !read(0,h.data(),h.size()))
                return reject(Status::malformed,"PFS : en-tête tronqué / lecture incomplète");
            if (le(h.data(),8)!=1 || le(h.data()+8,8)!=20130315)
                return reject(Status::unknown,"PFS : format inconnu (version/magic non reconnus), chiffrement non déterminé");
            mode = uint16_t(le(h.data()+0x1c,2));
            blockSize = uint32_t(le(h.data()+0x20,4));
            // The encryption bit describes the data after the clear superblock.
            // Stop here: never read an inode, seed, license or encrypted payload.
            if (mode & 4) return reject(Status::encrypted,
                "PFS chiffré déclaré : inaccessible ; eboot.bin ne peut pas être extrait avec les capacités actuelles");
            if (mode != 8 || le(h.data()+0x24,4) || le(h.data()+0x28,8)!=1)
                return reject(Status::unsupported,"PFS non chiffré déclaré : variante signée/64 bits/flags/backups non prise en charge");
            const auto count=le(h.data()+0x30,8), blocks=le(h.data()+0x38,8), tableBlocks=le(h.data()+0x40,8);
            if (blockSize<4096 || blockSize>65536 || (blockSize & (blockSize-1)) ||
                !count || count>4096 || !tableBlocks || tableBlocks>4096 ||
                !blocks || blocks>size/blockSize || size%blockSize || blocks!=size/blockSize ||
                tableBlocks!=(count+(blockSize/0xa8)-1)/(blockSize/0xa8) || tableBlocks>=blocks)
                return reject(Status::malformed,"PFS : tailles, compteurs ou table inode invalides");
            std::vector<Inode> inodes;
            std::vector<std::pair<uint64_t,uint64_t>> extents;
            for (uint64_t i=0;i<count;++i) {
                std::array<uint8_t,0xa8> b{};
                const auto at=blockSize+(i/(blockSize/0xa8))*blockSize+(i%(blockSize/0xa8))*0xa8;
                exact(read,at,b.data(),b.size());
                Inode n{uint16_t(le(b.data(),2)),le(b.data()+8,8),le(b.data()+0x60,4),le(b.data()+0x64,4)};
                const auto kind=n.mode & 0xf000;
                if ((kind!=0x4000 && kind!=0x8000) || (le(b.data()+4,4)&~uint64_t(0x20010)) ||
                    le(b.data()+0x10,8)!=n.size)
                    throw Failure(Status::unsupported,"PFS : inode spécial, comprimé ou flags inconnus refusé");
                if (n.blocks!=(n.size/blockSize+(n.size%blockSize!=0)) ||
                    (kind==0x4000 && !n.size) || (n.blocks && (n.start<=tableBlocks ||
                    !PS4PKGInventory::range(n.start,n.blocks,blocks))))
                    throw Failure(Status::malformed,"PFS : étendue inode invalide");
                for (size_t j=1;j<12;++j) {
                    const auto p=le(b.data()+0x64+j*4,4);
                    if (p && p!=0xffffffffU) throw Failure(Status::unsupported,"PFS : blocs non contigus refusés");
                }
                for (size_t j=0;j<5;++j) if (le(b.data()+0x94+j*4,4))
                    throw Failure(Status::unsupported,"PFS : blocs indirects refusés");
                for (const auto& e:extents) if (n.blocks && n.start<e.first+e.second && e.first<n.start+n.blocks)
                    throw Failure(Status::malformed,"PFS : étendues inode chevauchantes");
                if (n.blocks) extents.emplace_back(n.start,n.blocks);
                inodes.push_back(n);
            }
            std::set<uint32_t> visited;
            std::vector<File> pending;
            size_t entries=0, pathBytes=0;
            std::function<void(uint32_t,uint32_t,const std::string&,size_t)> walk;
            walk=[&](uint32_t index,uint32_t parent,const std::string& prefix,size_t depth) {
                if (depth>16 || index>=inodes.size() || !visited.insert(index).second ||
                    (inodes[index].mode&0xf000)!=0x4000)
                    throw Failure(Status::malformed,"PFS : cycle, profondeur ou inode répertoire invalide");
                const auto& n=inodes[index];
                std::vector<uint8_t> bytes;
                if (n.size>1024*1024) throw Failure(Status::unsupported,"PFS : répertoire supérieur à 1 Mio refusé");
                bytes.resize(size_t(n.size)); exact(read,n.start*blockSize,bytes.data(),bytes.size());
                std::set<std::string> names;
                for (size_t at=0;at<bytes.size();) {
                    const auto remaining=std::min<size_t>(bytes.size()-at,blockSize-at%blockSize);
                    if (remaining<16) throw Failure(Status::malformed,"PFS : dirent tronqué");
                    const auto* p=bytes.data()+at;
                    const auto ino=le(p,4),type=le(p+4,4),len=le(p+8,4),span=le(p+12,4);
                    if (!span) {
                        if (std::any_of(p,p+remaining,[](uint8_t c){return c!=0;}))
                            throw Failure(Status::malformed,"PFS : terminaison dirent invalide");
                        at+=remaining; continue;
                    }
                    if (++entries>4096 || ino>=inodes.size() || !len || len>255 || span%8 || span>remaining ||
                        span<17+len || p[16+len]!=0)
                        throw Failure(Status::malformed,"PFS : longueur, taille ou numéro dirent invalide");
                    std::string name(reinterpret_cast<const char*>(p+16),size_t(len));
                    if (!names.insert(name).second) throw Failure(Status::malformed,"PFS : nom dupliqué");
                    if (type==4 || type==5) {
                        if (name!=(type==4?".":"..") || ino!=(type==4?index:parent))
                            throw Failure(Status::malformed,"PFS : lien dot/dotdot invalide");
                    } else {
                        if (!component(name)) throw Failure(Status::malformed,"PFS : chemin malveillant refusé");
                        const auto path=prefix+name;
                        if (path.size()>4096) throw Failure(Status::malformed,"PFS : chemin trop long");
                        if (type==3) {
                            if (index==0 && name=="uroot") hasURoot=true;
                            walk(uint32_t(ino),index,path+"/",depth+1);
                        }
                        else if (type==2 && (inodes[ino].mode&0xf000)==0x8000 && visited.insert(uint32_t(ino)).second)
                            {
                            if (path.size()>1024*1024-pathBytes)
                                throw Failure(Status::unsupported,"PFS : budget de chemins 1 Mio dépassé");
                            pathBytes+=path.size();
                            pending.push_back({path,inodes[ino].start*blockSize,inodes[ino].size});
                        }
                        else throw Failure(Status::unsupported,"PFS : type dirent/lien non pris en charge");
                    }
                    at+=size_t(span);
                }
            };
            walk(0,0,"",0);
            // The reference exposes files under uroot. Do not infer app0 from
            // metadata or an arbitrary superroot file such as pfs_image.dat.
            if (!hasURoot) return reject(Status::malformed,"PFS : répertoire uroot absent");
            for (auto& f:pending) if (f.path.compare(0,6,"uroot/")==0) {
                f.path.erase(0,6); files.push_back(std::move(f));
            }
            std::sort(files.begin(),files.end(),[](const auto& a,const auto& b){return a.path<b.path;});
            status=Status::accessible;
            diagnostic="PFS non chiffré accessible : lecture seule, D32 contigu ; authenticité non vérifiée";
            return true;
        } catch (const Failure& f) { return reject(f.status,f.what()); }
        catch (const std::bad_alloc&) { return reject(Status::unsupported,"PFS : budget mémoire indisponible"); }
    }
    // Selected-file extraction to bounded memory, never a source-derived host
    // path. The caller chooses a destination; no filesystem writes here.
    bool extract(const std::string& path,const PS4PKGInventory::Reader& read,
                 std::vector<uint8_t>& out,std::string& why) const {
        out.clear(); why.clear();
        if (status!=Status::accessible || !allowedSelection(path)) { why="PFS non accessible"; return false; }
        const auto it=std::find_if(files.begin(),files.end(),[&](const auto& f){return f.path==path;});
        if (it==files.end() || it->size>64*1024*1024) { why="Fichier absent ou supérieur à 64 Mio"; return false; }
        try {
            std::vector<uint8_t> pending(size_t(it->size));
            if (it->size && !read(it->offset,pending.data(),pending.size())) { why="Lecture incomplète"; return false; }
            out=std::move(pending); return true;
        } catch (const std::bad_alloc&) { why="Budget mémoire indisponible"; return false; }
    }
    static bool allowedSelection(const std::string& path) {
        size_t start=0;
        do {
            const auto end=path.find('/',start);
            auto name=path.substr(start,end==std::string::npos?end:end-start);
            if (!component(name)) return false;
            for (auto& c:name) if (c>='A' && c<='Z') c=char(c+32);
            if (name=="keystone" || name=="license.dat" || name=="license.info" ||
                name=="entry_keys" || name=="image_key" || name==".rif" ||
                (name.size()>=4 && name.compare(name.size()-4,4,".rif")==0)) return false;
            if (end==std::string::npos) break;
            start=end+1;
        } while (true);
        return true;
    }
private:
    struct Inode { uint16_t mode; uint64_t size,blocks,start; };
    struct Failure:std::runtime_error { Status status; Failure(Status s,const char* t):std::runtime_error(t),status(s){} };
    uint64_t imageSize=0;
    size_t inspected=0;
    bool hasURoot=false;
    static uint64_t le(const uint8_t* p,size_t n) { uint64_t v=0; for(size_t i=0;i<n;++i) v|=uint64_t(p[i])<<(8*i); return v; }
    static bool component(const std::string& n) {
        return n!="." && n!=".." && !n.empty() && std::all_of(n.begin(),n.end(),[](unsigned char c){
            return c>=32 && c<127 && c!='/' && c!='\\' && c!=':';
        });
    }
    bool reject(Status s,const std::string& why) { files.clear(); status=s; diagnostic=why; return false; }
    void exact(const PS4PKGInventory::Reader& read,uint64_t at,uint8_t* p,size_t n) {
        if (n>8*1024*1024-inspected) throw Failure(Status::unsupported,"PFS : budget total de lecture 8 Mio dépassé");
        inspected+=n;
        if (!PS4PKGInventory::range(at,n,imageSize) || !read(at,p,n))
            throw Failure(Status::malformed,"PFS : lecture hors limites ou incomplète");
    }
};
}
