#pragma once
#include <algorithm>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>
#include <fstream>
#if defined(__linux__)
#include <dlfcn.h>
#endif
namespace maxps4 {
// Optional host tool backend. Xlib is loaded through its stable C ABI; no
// Linux dependency enters the portable VideoOut or iOS runtime.
class LinuxVideoPresenter {
    void* library = nullptr; void* display = nullptr; void* visual = nullptr;
    unsigned long window = 0; void* gc = nullptr; int depth = 0;
    uint32_t keys = 0; // buttons held on the keyboard
    template<class F> F symbol(const char* name) {
#if defined(__linux__)
        return reinterpret_cast<F>(dlsym(library, name));
#else
        (void)name; return nullptr;
#endif
    }
public:
    bool open(std::string& error) {
        if (display) return true;
#if defined(__linux__)
        library = dlopen("libX11.so.6", RTLD_NOW | RTLD_LOCAL);
        if (!library) { error = "libX11 unavailable"; return false; }
        auto connect = symbol<void*(*)(const char*)>("XOpenDisplay");
        display = connect ? connect(nullptr) : nullptr;
        if (!display) { error = "XOpenDisplay failed (DISPLAY/authentication/socket)"; return false; }
        const int screen = symbol<int(*)(void*)>("XDefaultScreen")(display);
        visual = symbol<void*(*)(void*,int)>("XDefaultVisual")(display, screen);
        depth = symbol<int(*)(void*,int)>("XDefaultDepth")(display, screen);
        if (depth != 24 && depth != 32) { error = "X11 visual depth unsupported"; return false; }
        const auto root = symbol<unsigned long(*)(void*)>("XDefaultRootWindow")(display);
        window = symbol<unsigned long(*)(void*,unsigned long,int,int,unsigned,unsigned,unsigned,unsigned long,unsigned long)>("XCreateSimpleWindow")
            (display, root, 0, 0, 960, 540, 0, 0, 0);
        gc = symbol<void*(*)(void*,unsigned long,unsigned long,void*)>("XCreateGC")(display, window, 0, nullptr);
        if (!window || !gc) { error = "X11 window/GC creation failed"; return false; }
        symbol<int(*)(void*,unsigned long,const char*)>("XStoreName")(display, window, "MaxPS4 - Kero Blaster - awaiting guest frame");
        symbol<int(*)(void*,unsigned long,long)>("XSelectInput")(display, window, 1L /*KeyPressMask*/ | 2L /*KeyReleaseMask*/);
        symbol<int(*)(void*,unsigned long)>("XMapWindow")(display, window);
        symbol<int(*)(void*,int)>("XSync")(display, 0);
        return true;
#else
        error = "X11 host backend is Linux only"; return false;
#endif
    }
    bool present(uint32_t width, uint32_t height, const std::vector<uint8_t>& rgba, std::string& error) {
        if (!display || !window || !gc || !width || !height || width > 4096 || height > 4096 || rgba.size() != size_t(width)*height*4) {
            error = "invalid X11 presentation state/frame"; return false;
        }
        char* data = static_cast<char*>(std::calloc(size_t(width)*height, 4));
        if (!data) { error = "frame allocation failed"; return false; }
        auto image = symbol<void*(*)(void*,void*,unsigned,int,int,char*,unsigned,unsigned,int,int)>("XCreateImage")
            (display, visual, unsigned(depth), 2, 0, data, width, height, 32, 0);
        if (!image) { std::free(data); error = "XCreateImage failed"; return false; }
        auto putPixel = symbol<int(*)(void*,int,int,unsigned long)>("XPutPixel");
        for (uint32_t y=0; y<height; ++y) for (uint32_t x=0; x<width; ++x) {
            const auto* p = &rgba[(size_t(y)*width+x)*4];
            putPixel(image, int(x), int(y), (static_cast<unsigned long>(p[0])<<16) | (static_cast<unsigned long>(p[1])<<8) | p[2]);
        }
        symbol<int(*)(void*,unsigned long,unsigned,unsigned)>("XResizeWindow")(display, window, width, height);
        symbol<int(*)(void*,unsigned long,void*,void*,int,int,int,int,unsigned,unsigned)>("XPutImage")
            (display, window, gc, image, 0,0,0,0,width,height);
        symbol<int(*)(void*,int)>("XSync")(display,0);
        symbol<int(*)(void*)>("XDestroyImage")(image);
        return true;
    }
    // Keyboard -> PS4 pad buttons (SCE_PAD_BUTTON_* bits), from pending X events.
    // Arrows: D-pad; Z: cross, X: square, C: circle, V: triangle, A/S: L1/R1,
    // Q/W: L2/R2, Return: options, Tab: touch pad.
    static uint32_t buttonForKeysym(unsigned long keysym) {
        switch (keysym) {
        case 0xff52: return 0x10;   case 0xff53: return 0x20;  case 0xff54: return 0x40;  case 0xff51: return 0x80; // Up Right Down Left
        case 'z': return 0x4000;    case 'x': return 0x8000;   case 'c': return 0x2000;   case 'v': return 0x1000;
        case 'a': return 0x400;     case 's': return 0x800;    case 'q': return 0x100;    case 'w': return 0x200;
        case 0xff0d: return 0x8;    case 0xff09: return 0x100000;
        default: return 0;
        }
    }
    uint32_t pollButtons() {
        if (!display) return keys;
        auto pending = symbol<int(*)(void*)>("XPending");
        auto next = symbol<int(*)(void*,void*)>("XNextEvent");
        auto lookup = symbol<unsigned long(*)(void*,int)>("XLookupKeysym");
        if (!pending || !next || !lookup) return keys;
        alignas(8) unsigned char event[192];
        while (pending(display) > 0) {
            next(display, event);
            int type; std::memcpy(&type, event, sizeof(type));
            if (type != 2 && type != 3) continue; // KeyPress, KeyRelease
            const uint32_t bit = buttonForKeysym(lookup(event, 0));
            if (type == 2) keys |= bit; else keys &= ~bit;
        }
        return keys;
    }
    void close() {
        if (display) {
            if (gc) symbol<int(*)(void*,void*)>("XFreeGC")(display,gc);
            if (window) symbol<int(*)(void*,unsigned long)>("XDestroyWindow")(display,window);
            symbol<int(*)(void*)>("XCloseDisplay")(display);
        }
        display=nullptr; window=0; gc=nullptr;
    }
    ~LinuxVideoPresenter() {
        close();
#if defined(__linux__)
        if (library) dlclose(library);
#endif
    }
    // PNG with uncompressed DEFLATE. It writes only bytes supplied by the
    // caller, with no generated artwork, placeholder frame or external tool.
    static bool png(const std::string& path, uint32_t w, uint32_t h, const std::vector<uint8_t>& rgba) {
        if (!w || !h || rgba.size()!=size_t(w)*h*4) return false;
        std::vector<uint8_t> out{137,80,78,71,13,10,26,10};
        auto be=[](std::vector<uint8_t>& b,uint32_t n){for(int s=24;s>=0;s-=8)b.push_back(uint8_t(n>>s));};
        auto chunk=[&](const char* tag,const std::vector<uint8_t>& bytes){
            be(out,uint32_t(bytes.size()));size_t start=out.size();out.insert(out.end(),tag,tag+4);out.insert(out.end(),bytes.begin(),bytes.end());
            uint32_t crc=~0U;for(size_t i=start;i<out.size();++i){crc^=out[i];for(int k=0;k<8;++k)crc=(crc>>1)^(0xedb88320U&uint32_t(-int(crc&1)));}be(out,~crc);
        };
        std::vector<uint8_t> header;be(header,w);be(header,h);header.insert(header.end(),{8,6,0,0,0});chunk("IHDR",header);
        std::vector<uint8_t> raw;raw.reserve((size_t(w)*4+1)*h);
        for(uint32_t y=0;y<h;++y){raw.push_back(0);auto p=rgba.begin()+size_t(y)*w*4;raw.insert(raw.end(),p,p+size_t(w)*4);}
        std::vector<uint8_t> z{0x78,0x01};uint32_t a=1,b=0;
        for(auto v:raw){a=(a+v)%65521;b=(b+a)%65521;}
        for(size_t at=0;at<raw.size();){uint16_t n=uint16_t(std::min<size_t>(65535,raw.size()-at));z.push_back(at+n==raw.size()?1:0);z.push_back(uint8_t(n));z.push_back(uint8_t(n>>8));z.push_back(uint8_t(~n));z.push_back(uint8_t((~n)>>8));z.insert(z.end(),raw.begin()+at,raw.begin()+at+n);at+=n;}
        be(z,(b<<16)|a);chunk("IDAT",z);chunk("IEND",{});
        std::ofstream f(path,std::ios::binary);f.write(reinterpret_cast<const char*>(out.data()),std::streamsize(out.size()));return bool(f);
    }
};
}
