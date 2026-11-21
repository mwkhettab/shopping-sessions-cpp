#include "sys.hpp"
#include <atomic>
#include <bit>
#include <cstdlib>
#include <new>

namespace {
// new[] and library code routinely use unsized delete, including for over-aligned
// objects. Store the class outside each allocation so power-of-two vectors still
// fit their original size class. Both indexes consume physical pages on demand.
using namespace pipeline;
using namespace pipeline::sys;
constexpr std::size_t small_count=A_REGION/16, page_count=A_REGION/4096;
constinit std::atomic<u8*> class_index{};

u8* get_index() noexcept {
    auto* index=class_index.load(std::memory_order_acquire);
    if (index) return index;
    auto* area=::mmap(nullptr,small_count+page_count,PROT_READ|PROT_WRITE,anonymous_flags,-1,0);
    if (area==MAP_FAILED) return nullptr;
    auto* candidate=static_cast<u8*>(area);
    advise_pages(candidate,small_count+page_count,false);
    if (class_index.compare_exchange_strong(index,candidate,std::memory_order_release,std::memory_order_acquire)) return candidate;
    ::munmap(area,small_count+page_count);
    return index;
}
inline u8& tag(u8* index,std::size_t offset,unsigned cls) noexcept {
    return cls>=12 ? index[small_count+(offset>>12)] : index[offset>>4];
}
void* allocate(std::size_t n,std::size_t alignment) {
    // Zero-byte allocation must still return a distinct, suitably aligned object.
    n=std::max<std::size_t>(n,1);
    for (;;) {
        const auto base=A_BASE.load(std::memory_order_acquire);
        if (base && alignment<=4096) {
            if (auto* index=get_index()) {
                if (void* p=RegionResource::try_region(n,alignment,false)) {
                    const auto cls=unsigned(a_class(n,alignment));
                    const auto addr=reinterpret_cast<std::uintptr_t>(p);
                    tag(index,addr-base,cls)=u8(cls);
                    protect_managed(addr,std::size_t{1}<<cls);
                    return p;
                }
            }
        }
        if (void* p=system_allocate(n,alignment,false)) return p;
        if (auto handler=std::get_new_handler()) handler();
        else throw std::bad_alloc{};
    }
}
void deallocate(void* p) noexcept {
    if (!p) return;
    if (!a_mine(p)) {std::free(p);return;}
    const auto offset=reinterpret_cast<std::uintptr_t>(p)-A_BASE.load(relaxed);
    const auto index=class_index.load(std::memory_order_acquire);
    if (!index) std::abort();
    // No two different size classes ever reuse the same address in the region.
    // A block shorter than a page can start on a page boundary.
    unsigned cls=0;
    if (!(offset&4095)) cls=index[small_count+(offset>>12)];
    if (!cls) cls=index[offset>>4];
    if (cls<4 || cls>=std::numeric_limits<std::size_t>::digits) std::abort();
    auto& head=local_alloc.free[cls];
    std::memcpy(p,&head,sizeof(head));head=reinterpret_cast<std::uintptr_t>(p);
}
void* try_allocate(std::size_t n,std::size_t alignment) noexcept {
    try {return allocate(n,alignment);} catch(...) {return nullptr;}
}
constexpr std::size_t default_alignment=__STDCPP_DEFAULT_NEW_ALIGNMENT__;
}

void* operator new(std::size_t n) {return allocate(n,default_alignment);}
void* operator new[](std::size_t n) {return allocate(n,default_alignment);}
void* operator new(std::size_t n,std::align_val_t a) {return allocate(n,static_cast<std::size_t>(a));}
void* operator new[](std::size_t n,std::align_val_t a) {return allocate(n,static_cast<std::size_t>(a));}
void* operator new(std::size_t n,const std::nothrow_t&) noexcept {return try_allocate(n,default_alignment);}
void* operator new[](std::size_t n,const std::nothrow_t&) noexcept {return try_allocate(n,default_alignment);}
void* operator new(std::size_t n,std::align_val_t a,const std::nothrow_t&) noexcept {return try_allocate(n,static_cast<std::size_t>(a));}
void* operator new[](std::size_t n,std::align_val_t a,const std::nothrow_t&) noexcept {return try_allocate(n,static_cast<std::size_t>(a));}

void operator delete(void* p) noexcept {deallocate(p);}
void operator delete[](void* p) noexcept {deallocate(p);}
void operator delete(void* p,std::size_t) noexcept {deallocate(p);}
void operator delete[](void* p,std::size_t) noexcept {deallocate(p);}
void operator delete(void* p,std::align_val_t) noexcept {deallocate(p);}
void operator delete[](void* p,std::align_val_t) noexcept {deallocate(p);}
void operator delete(void* p,std::size_t,std::align_val_t) noexcept {deallocate(p);}
void operator delete[](void* p,std::size_t,std::align_val_t) noexcept {deallocate(p);}
void operator delete(void* p,const std::nothrow_t&) noexcept {deallocate(p);}
void operator delete[](void* p,const std::nothrow_t&) noexcept {deallocate(p);}
void operator delete(void* p,std::align_val_t,const std::nothrow_t&) noexcept {deallocate(p);}
void operator delete[](void* p,std::align_val_t,const std::nothrow_t&) noexcept {deallocate(p);}
