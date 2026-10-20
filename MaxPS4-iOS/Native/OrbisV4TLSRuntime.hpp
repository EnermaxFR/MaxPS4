#pragma once
#include "OrbisPRXFEXBridge.hpp"
#include <array>
#include <mutex>

namespace maxps4 {
// Immutable, eagerly allocated TLS registry. Known TCB self/DTV fields only;
// no scheduler, pthread object, canary, fibers or dynamic module loading.
class OrbisV4TLSRuntime {
    static constexpr uint64_t returnPC=0x700000020000ULL;
    static constexpr uint64_t base=0x710000000000ULL,stride=0x400000;
    struct Description {uint64_t serial=0,base=0,fs=0,dtv=0,rsp=0;size_t bytes=0;};
    struct State {
        std::mutex mutex;
        std::unique_ptr<OrbisV4Translation> memory;
        std::vector<OrbisV4TLSModule> modules;
        std::vector<OrbisV4TLSPlacement> placements;
        std::array<Description,16> threads{};
        size_t total=0;
        uint32_t maxId=0;
        uint64_t next=1;
        bool closed=false;
    };
public:
    class Thread {
        std::weak_ptr<State> state_;
        uint64_t serial_=0;
        friend class OrbisV4TLSRuntime;
    public:
        Thread()=default;
    };
private:
    std::shared_ptr<State> state_;
    Thread primary_;
    explicit OrbisV4TLSRuntime(std::shared_ptr<State> state):state_(std::move(state)){}
    static const Description& find(const State& state,const Thread& thread) {
        if(state.closed || thread.state_.lock().get()!=&state || !thread.serial_)
            throw std::logic_error("TLS closed/stale/foreign thread");
        for(const auto& item:state.threads)if(item.serial==thread.serial_)return item;
        throw std::logic_error("TLS thread retired");
    }
    int create(Thread& output) {
        auto& state=*state_;
        if(state.closed)return -ESHUTDOWN;
        if(state.next==UINT64_MAX)return -EOVERFLOW;
        size_t slot=0;while(slot<state.threads.size() && state.threads[slot].serial)++slot;
        if(slot==state.threads.size())return -EAGAIN;
        const auto page=state.memory->page_size();
        const size_t dataBytes=(std::max<size_t>(state.total,1)+page-1)/page*page;
        const size_t bytes=dataBytes+2*page;
        const uint64_t begin=base+slot*stride,fs=begin+dataBytes,dtv=fs+64;
        if(bytes+2*page>stride || 64+(state.maxId+2)*8>page)return -E2BIG;
        // Finish every host allocation before reserving guest storage.
        std::vector<uint8_t> initial,metadata,stack(page,0);
        int status;
        if(!state.modules.empty()) {
            status=OrbisV4TLSLayout::instantiate(state.modules,state.placements,state.total,initial);
            if(status)return status;
        }
        metadata.resize(page,0);
        auto put=[&](size_t offset,uint64_t value){for(size_t i=0;i<8;++i)metadata.at(offset+i)=uint8_t(value>>(i*8));};
        put(0,fs);put(8,dtv); // thread/fiber/canary absent in this profile
        put(64,1);put(72,state.maxId);
        for(const auto& placement:state.placements)put(64+(placement.id+1)*8,begin+placement.offset);
        for(size_t i=0;i<8;++i)stack[page-8+i]=uint8_t(returnPC>>(i*8));
        status=state.memory->reserve(begin,bytes);
        if(status)return status;
        status=state.memory->map(begin,dataBytes,6,initial.empty() ? nullptr : initial.data(),initial.size());
        if(!status)status=state.memory->map(fs,page,4,metadata.data(),metadata.size());
        if(!status)status=state.memory->map(fs+page,page,6,stack.data(),stack.size());
        if(status){state.memory->release(begin);return status;}
        const uint64_t serial=state.next++;
        state.threads[slot]={serial,begin,fs,dtv,fs+2*page-8,bytes};
        Thread result;result.state_=state_;result.serial_=serial;output=std::move(result);
        return 0;
    }
public:
    OrbisV4TLSRuntime(const OrbisV4TLSRuntime&)=delete;
    OrbisV4TLSRuntime& operator=(const OrbisV4TLSRuntime&)=delete;
    ~OrbisV4TLSRuntime(){close();}
    // Publication occurs after closed-graph linkage, final permissions and main
    // thread initialization. This grants no process/CPU admission.
    static int load(const OrbisLinker::TLSImage& image,OrbisV4Translation::Invalidator invalidate,
                    std::unique_ptr<OrbisV4TLSRuntime>& output,std::string& reason) {
        try {
            OrbisPRXFEXBridge bridge;
            if(!bridge.map(image)){reason="TLS.RUNTIME: "+bridge.diagnostic();return -ENOTSUP;}
            auto state=std::make_shared<State>();
            state->memory=bridge.make_translation(std::move(invalidate));
            const auto page=state->memory->page_size();
            int prepared=state->memory->reserve(returnPC,page);
            const uint8_t halt=0xf4;
            if(!prepared)prepared=state->memory->map(returnPC,page,5,&halt,1);
            if(prepared){reason="TLS.RUNTIME: return gate collision";return prepared;}
            state->modules=image.plan().templates;state->placements=image.plan().placements;state->total=image.plan().total;
            for(const auto& module:image.plan().modules)state->maxId=std::max(state->maxId,module.id);
            auto pending=std::unique_ptr<OrbisV4TLSRuntime>(new OrbisV4TLSRuntime(std::move(state)));
            const int status=pending->create(pending->primary_);
            if(status){reason="TLS.RUNTIME: main-thread initialization status="+std::to_string(status);return status;}
            reason="TLS.RUNTIME: linked modules + main TCB/DTV; static eager registry; CPU_admitted=false";
            output=std::move(pending);return 0;
        } catch(const std::bad_alloc&) {reason="TLS.RUNTIME: allocation failed";return -ENOMEM;}
          catch(const std::exception& e) {reason="TLS.RUNTIME: "+std::string(e.what());return -ENOTSUP;}
    }
    Thread primary() const {return primary_;}
    // Mission 50: process memory mapped on behalf of kernel services (flexible
    // or direct memory). Never callable while a Lease is held (same mutex), so
    // no dispatch is running; the translation invalidates JIT code first.
    int set_memory_limit(size_t bytes) {
        std::lock_guard lock(state_->mutex);
        if(state_->closed)return -ESHUTDOWN;
        return state_->memory->set_limit(bytes);
    }
    int map_anonymous(uint64_t guest,size_t bytes,uint32_t permissions) {
        std::lock_guard lock(state_->mutex);
        if(state_->closed)return -ESHUTDOWN;
        int status=state_->memory->reserve(guest,bytes);
        if(status)return status;
        status=state_->memory->map(guest,bytes,permissions);
        if(status)state_->memory->release(guest);
        return status;
    }
    int protect(uint64_t guest,size_t bytes,uint32_t permissions) {
        std::lock_guard lock(state_->mutex);
        if(state_->closed)return -ESHUTDOWN;
        return state_->memory->protect(guest,bytes,permissions);
    }
    int release_mapping(uint64_t guest) {
        std::lock_guard lock(state_->mutex);
        if(state_->closed)return -ESHUTDOWN;
        return state_->memory->release(guest);
    }
    size_t page_size() const {return state_->memory->page_size();}
    // Memory size of TLS module 'id' (0 = unknown module). The module list is
    // fixed when the runtime is created, so no lock: callers run while a Lease
    // (which holds state_->mutex) is active, e.g. __tls_get_addr.
    size_t module_size(uint64_t id) const {
        for(const auto& module:state_->modules)if(module.id==id)return module.memory_size;
        return 0;
    }
    int create_thread(Thread& output) {
        std::lock_guard lock(state_->mutex);
        try {return create(output);}catch(const std::bad_alloc&){return -ENOMEM;}
    }
    int release_thread(const Thread& thread) {
        std::lock_guard lock(state_->mutex);
        if(state_->closed)return -ESHUTDOWN;
        if(thread.state_.lock()!=state_)return -EINVAL;
        if(thread.serial_==primary_.serial_)return -EBUSY;
        for(auto& item:state_->threads)if(item.serial && item.serial==thread.serial_) {
            const int status=state_->memory->release(item.base);
            if(!status)item={};
            return status;
        }
        return -ENOENT;
    }
    class Lease {
        std::shared_ptr<State> state_;
        std::unique_lock<std::mutex> lock_;
        Description thread_;
        OrbisV4Translation::Lease memory_;
        friend class OrbisV4TLSRuntime;
        Lease(std::shared_ptr<State> state,const Thread& thread):state_(std::move(state)),lock_(state_->mutex),
            thread_(find(*state_,thread)),memory_(*state_->memory){}
    public:
        OrbisV4Translation::Lease& memory() {return memory_;}
        const OrbisV4Translation::Lease& memory() const {return memory_;}
        uint64_t fs_base() const {return thread_.fs;}
        uint64_t rsp() const {return thread_.rsp;}
        int resolve(uint64_t id,uint64_t offset,size_t count,uint64_t& address) const {
            if(!id || id>state_->maxId || !count)return -EINVAL;
            const OrbisV4TLSPlacement* placement=nullptr;
            for(const auto& p:state_->placements)if(p.id==id)placement=&p;
            if(!placement)return -ENOENT; // no lazy/dynamic allocation
            if(offset>=placement->size || count>placement->size-offset)return -EFAULT;
            uint64_t self=0,dtv=0,generation=0,maximum=0,entry=0;
            if(memory_.read_data(thread_.fs,&self,8) || memory_.read_data(thread_.fs+8,&dtv,8) ||
               self!=thread_.fs || dtv!=thread_.dtv || memory_.read_data(dtv,&generation,8) ||
               generation!=1 || memory_.read_data(dtv+8,&maximum,8) || maximum!=state_->maxId ||
               memory_.read_data(dtv+(id+1)*8,&entry,8) || entry!=thread_.base+placement->offset ||
               !memory_.permitted(entry+offset,count,6))return -EFAULT;
            address=entry+offset;return 0;
        }
    };
    Lease lease(const Thread& thread){return Lease(state_,thread);}
    void close() {
        if(!state_)return;
        std::lock_guard lock(state_->mutex);
        if(state_->closed)return;
        state_->memory->close();state_->closed=true;state_->threads={};
    }
};
}
