// Mission 50: real-title boot runner (host development tool, FEX JIT).
// Reads clear SELF/PRX files read-only, links the complete batch, maps it
// through the TLS runtime and executes, under FEX, the rtld sequence observed
// in the binaries: libc DT_INIT(0,0,0), Fios2 DT_INIT(0,0,0), then the eboot
// entry with {argc,argv} and an exit handler. Every service call is logged;
// an unimplemented service stops execution (TRAP), never returns a value.
// Linux runner for one Orbis V4 title (OrbisV4GameSession.hpp): X11 window,
// PulseAudio/WAV output, PNG captures and scripted controller input.
#include "OrbisV4GameSession.hpp"
#include "LinuxVideoPresenter.hpp"
#include "LinuxAudioSink.hpp"
#include "PadInputScript.hpp"
#include <filesystem>
#include <fstream>
#include <iostream>
#include <iterator>
#include <sstream>

namespace {
std::vector<uint8_t> slurp(const std::string& path) {
    std::ifstream f(path, std::ios::binary); return {std::istreambuf_iterator<char>(f), {}};
}
}

int main(int argc, char** argv) {
    using namespace maxps4;
    std::cout << std::unitbuf;
    if (argc < 5) { std::cerr << "usage: boot <names.tsv> <eboot.bin> <libc.prx> <libSceFios2.prx> [app0-dir]\n"; return 2; }
    OrbisV4GameFiles game;
    {
        std::ifstream t(argv[1]); std::string line; std::getline(t, line);
        while (std::getline(t, line)) {
            std::vector<std::string> f; std::stringstream ss(line); std::string x;
            while (std::getline(ss, x, '\t')) f.push_back(x);
            if (f.size() > 8 && !f[8].empty() && f[8] != "UNKNOWN") game.names[f[7]] = f[8];
        }
    }
    for (int i = 2; i < 5; ++i) { std::string n = argv[i]; game.modules.push_back({n.substr(n.find_last_of('/') + 1), slurp(n)}); }
    game.app0 = argc > 5 ? argv[5] : "";
    OrbisV4GameHost host{std::cout};
    // Audio: MAXPS4_AUDIO_WAV=path captures the title's output; MAXPS4_AUDIO_PULSE=1 plays it.
    auto speaker=std::make_shared<LinuxAudioSink>();
    if(const char* wav=std::getenv("MAXPS4_AUDIO_WAV"); wav && !speaker->openWav(wav,48000))std::cout<<"AUDIO.WAV open failed: "<<wav<<'\n';
    if(std::getenv("MAXPS4_AUDIO_PULSE")){std::string why;std::cout<<(speaker->openPulse(48000,why)?"AUDIO.PULSE playing\n":"AUDIO.PULSE unavailable: "+why+"\n");}
    host.audio=[speaker](const int16_t* pcm,size_t frames){speaker->write(pcm,frames);};
    host.summary=[speaker](std::ostream& out){
        out<<"AUDIO.SINK captured_frames="<<speaker->frames()<<" pulse_errors="<<speaker->pulseErrors()<<'\n';
        speaker->close();
    };
    auto presenter=std::make_shared<LinuxVideoPresenter>();
    // MAXPS4_HEADLESS=1: no X11 window (long unattended sessions survive a lost
    // display); VideoOut presentation, hashing and PNG captures are unchanged.
    const bool headless=std::getenv("MAXPS4_HEADLESS")!=nullptr;
    host.open=[presenter,headless](std::string& error){return headless || presenter->open(error);};
    host.close=[presenter]{presenter->close();};
    // Controller: X11 keyboard plus MAXPS4_INPUT_SCRIPT (buttons by presented frame).
    auto script=std::make_shared<PadInputScript>();
    if(const char* text=std::getenv("MAXPS4_INPUT_SCRIPT")){std::string why;if(!script->parse(text,why)){std::cerr<<"MAXPS4_INPUT_SCRIPT: "<<why<<'\n';return 1;}}
    host.present=[presenter,script,headless,captured=false,frame=uint64_t(0),held=uint32_t(0)](uint32_t w,uint32_t h,const std::vector<uint8_t>& rgba,uint64_t guest,uint64_t arg,uint32_t& buttons,std::string& error) mutable {
        if(!headless && !presenter->present(w,h,rgba,error))return false;
        std::cout<<"VIDEO.PRESENT width="<<w<<" height="<<h<<" guest=0x"<<std::hex<<guest<<std::dec<<" flip="<<arg<<'\n';
        // MAXPS4_FRAME_EVERY=N: also keep every Nth presented frame as PNG.
        ++frame;
        // MAXPS4_INPUT_FILE: same syntax as MAXPS4_INPUT_SCRIPT, re-read whenever the file
        // changes, so one running session can be steered frame by frame.
        if(const char* file=std::getenv("MAXPS4_INPUT_FILE")){
            static std::filesystem::file_time_type seen{};
            std::error_code ec; const auto stamp=std::filesystem::last_write_time(file,ec);
            if(!ec && stamp!=seen){
                seen=stamp; std::ifstream in(file); std::string text((std::istreambuf_iterator<char>(in)),{}),why;
                for(auto& c:text) if(c=='\n'||c=='\r') c=';';
                PadInputScript next; if(next.parse(text,why)){*script=next;std::cout<<"PAD.SCRIPT reloaded at frame "<<frame<<" steps="<<script->steps.size()<<'\n';}
                else std::cout<<"PAD.SCRIPT rejected at frame "<<frame<<": "<<why<<'\n';
            }
        }
        std::cout<<"PAD.FRAME "<<frame<<'\n';
        buttons=(headless?0u:presenter->pollButtons())|script->buttonsAt(frame+1); // state for the next frame
        if(buttons!=held){std::cout<<"PAD.INPUT frame="<<frame+1<<" buttons=0x"<<std::hex<<buttons<<std::dec<<'\n';held=buttons;}
        if(const char* every=std::getenv("MAXPS4_FRAME_EVERY"); every && std::strtoull(every,nullptr,10) && frame%std::strtoull(every,nullptr,10)==0){
            const std::string dir=std::getenv("MAXPS4_FRAME_DIR")?std::getenv("MAXPS4_FRAME_DIR"):"docs/mission52-frames";
            char name[64];std::snprintf(name,sizeof(name),"/frame-%05llu.png",(unsigned long long)frame);
            if(!LinuxVideoPresenter::png(dir+name,w,h,rgba)){error="PNG capture failed";return false;}
        }
        if(!captured){
            const std::string dir=std::getenv("MAXPS4_FRAME_DIR")?std::getenv("MAXPS4_FRAME_DIR"):"docs/mission52-frames";
            if(!LinuxVideoPresenter::png(dir+"/first-guest-frame.png",w,h,rgba)){error="PNG capture failed";return false;}
            std::ofstream raw(dir+"/first-guest-frame.rgba",std::ios::binary);raw.write(reinterpret_cast<const char*>(rgba.data()),std::streamsize(rgba.size()));
            std::ofstream meta(dir+"/first-guest-frame.json");meta<<"{\"origin\":\"sceVideoOutSubmitFlip guest framebuffer snapshot\",\"guest_address\":"<<guest<<",\"width\":"<<w<<",\"height\":"<<h<<",\"format\":\"RGBA8\",\"flip_arg\":"<<arg<<"}\n";
            captured=true;
        }
        return true;
    };
    return runOrbisV4Game(game, host);
}
