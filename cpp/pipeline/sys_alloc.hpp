#pragma once
#include <memory_resource>
namespace pipeline::sys {
struct LocalAlloc {
    std::size_t cur{},end{};
    std::array<std::size_t,64> free{};
};
inline thread_local LocalAlloc local_alloc;
inline std::size_t a_class(std::size_t size,std::size_t alignment) {
    return std::bit_width(std::max<std::size_t>({size,alignment,16})-1);
}
inline bool a_mine(void *p) {
    const auto base=A_BASE.load(relaxed), n=reinterpret_cast<std::uintptr_t>(p);
    return base && n>=base && n<A_END.load(relaxed);
}
inline void* system_allocate(std::size_t n,std::size_t al,bool zero) noexcept {
    n=std::max<std::size_t>(n,1);
    if (!std::has_single_bit(al)) return nullptr;
    if (al<=alignof(std::max_align_t)) return zero ? std::calloc(1,n) : std::malloc(n);
    void* p=nullptr;
    if (::posix_memalign(&p,al,n)) return nullptr;
    if (zero) std::memset(p,0,n);
    return p;
}
class RegionResource final:public std::pmr::memory_resource {
    void *do_allocate(std::size_t n,std::size_t al) override {return get(n,al,false);}
    void do_deallocate(void *p,std::size_t n,std::size_t al) override {deallocate_block(p,n,al);}
    bool do_is_equal(const memory_resource &rhs) const noexcept override {return this==&rhs;}
public:
    static void *try_region(std::size_t n,std::size_t al,bool zero) noexcept {
        const auto c=a_class(n,al);
        if (A_BASE.load(std::memory_order_acquire) && al<=4096 && std::has_single_bit(al) && c<sizeof(std::size_t)*8) {
            auto &tl=local_alloc;
            const auto sz=std::size_t{1}<<c;
            if (auto h=tl.free[c]) {
                std::memcpy(&tl.free[c],reinterpret_cast<void*>(h),sizeof(h));
                if (zero) std::memset(reinterpret_cast<void*>(h),0,n);
                return reinterpret_cast<void*>(h);
            }
            if (c<=A_SMALL_MAX) {
                auto align=std::min<std::size_t>(sz,4096);
                auto a=(tl.cur+align-1)&~(align-1);
                if (!tl.cur || a+sz>tl.end) {
                    auto ch=a_region(A_CHUNK,A_CHUNK);
                    if (ch) {
                        if (!tl.cur && FIRST_SMALL.load(relaxed) && HUGE_PAGES.load(relaxed))
                            advise_pages(reinterpret_cast<void*>(ch),A_CHUNK,false);
                        tl.cur=ch; tl.end=ch+A_CHUNK; a=ch;
                    } else a=0;
                }
                if (a) {tl.cur=a+sz; return reinterpret_cast<void*>(a);}
            } else if (auto addr=a_region(sz,sz>=HP?HP:4096)) return reinterpret_cast<void*>(addr);
        }
        return nullptr;
    }
    static void *get(std::size_t n,std::size_t al,bool zero) {
        if (auto* p=try_region(n,al,zero)) {
            protect_managed(reinterpret_cast<std::uintptr_t>(p),std::size_t{1}<<a_class(n,al));
            return p;
        }
        if (auto* p=system_allocate(n,al,zero)) return p;
        throw std::bad_alloc{};
    }
    static void deallocate_block(void *p,std::size_t n,std::size_t al) {
        if (a_mine(p)) {
            auto &head=local_alloc.free[a_class(n,al)];
            std::memcpy(p,&head,sizeof(head));
            head=reinterpret_cast<std::uintptr_t>(p);
        } else std::free(p);
    }
};
inline RegionResource region_resource;
inline void prefault_chunk() {
    if (!A_BASE.load(relaxed)) return;
    auto *p=RegionResource::get(64,alignof(u64),false);
    RegionResource::deallocate_block(p,64,alignof(u64));
    const auto &tl=local_alloc;
    auto at=(tl.cur+4095)&~std::size_t{4095};
    const auto step=HUGE_PAGES.load(relaxed)?HP:4096;
    while (at<tl.end) {
        *reinterpret_cast<volatile u8*>(at)=0;
        at=(at+step)&~(step-1);
    }
}
}
