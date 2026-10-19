#pragma once
#include "OrbisMetadata.hpp"
namespace maxps4 {
// JSONL inventory of the supplied module only. This cannot discover imports of
// absent modules and makes no claim that the transitive graph is complete.
struct OrbisV4SystemInventory {
    static std::string quote(const std::string& value) {
        std::string out="\"";
        constexpr char hex[]="0123456789abcdef";
        for(unsigned char c:value){
            if(c=='"' || c=='\\'){out+='\\';out+=char(c);}
            else if(c<32 || c>=127){out+="\\u00";out+=hex[c>>4];out+=hex[c&15];}
            else out+=char(c);
        }
        return out+'"';
    }
    static std::string json(const OrbisMetadata& m,const std::string& file) {
        std::string out="{\"file\":"+quote(file)+",\"needed\":[";
        bool first=true;
        auto comma=[&]{if(!first)out+=',';first=false;};
        for(const auto& dep:m.needed){comma();out+=quote(dep);}
        out+="],\"modules\":[";first=true;
        for(const auto& mod:m.modules){comma();out+="{\"name\":"+quote(mod.name)+",\"version\":"+
            std::to_string(mod.version)+",\"exported\":"+(mod.exported ? "true" : "false")+"}";}
        out+="],\"libraries\":[";first=true;
        for(const auto& lib:m.libraries){comma();out+="{\"name\":"+quote(lib.name)+",\"version\":"+
            std::to_string(lib.version)+",\"exported\":"+(lib.exported ? "true" : "false")+"}";}
        out+="],\"imports\":[";first=true;
        for(const auto& symbol:m.symbols){
            if(symbol.defined || symbol.encoded.empty())continue;
            comma();out+="{\"encoded\":"+quote(symbol.encoded)+",\"nid\":"+quote(symbol.nid)+",\"module\":"+quote(symbol.module)+
                ",\"library\":"+quote(symbol.library)+",\"module_version\":"+std::to_string(symbol.moduleVersion)+
                ",\"library_version\":"+std::to_string(symbol.libraryVersion)+
                ",\"kind\":"+std::to_string(unsigned(symbol.kind))+",\"binding\":"+std::to_string(unsigned(symbol.binding))+
                ",\"qualified\":"+(symbol.qualified ? "true" : "false")+"}";
        }
        return out+"]}\n";
    }
};
}
