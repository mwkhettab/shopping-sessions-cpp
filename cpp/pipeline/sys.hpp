#pragma once
#include "model_types.hpp"
#include <atomic>
#include <cerrno>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <new>
#include <optional>
#include <span>
#include <string>
#include <sys/mman.h>
#include <thread>
#include <type_traits>
#include <unistd.h>
#include <vector>
#if defined(__linux__)
#include <sched.h>
#endif

namespace pipeline::sys {
inline constexpr std::size_t HP = 2u << 20, A_SMALL_MAX = 16, A_CHUNK = 4u << 20;
inline constexpr std::size_t A_REGION = std::size_t{64} << 30;
inline constexpr auto relaxed = std::memory_order_relaxed;
inline std::atomic_bool HUGE_PAGES{true}, SMALLPAGES{}, RELEASE{true}, ANON_REGION{true}, ANON_TINY{},
                        PREFAULT{}, FIRST_SMALL{}, PIN_AT_START{}, WORKERS{}, CORE_NOTOUCH{true}, SETUP_PARTS{true};
inline std::array<std::atomic_bool,16> PINNED{};
inline std::atomic<u64> SPIN_TICKS{100000};
inline std::atomic<std::size_t> SETUP_KP{}, A_BASE{}, A_END{}, A_NEXT{};

// A page with live C++ storage must survive the final output-page release. New
// allocations wait only if another thread is discarding that particular page.
inline constexpr u8 A_UNCLAIMED=0,A_MANAGED_PAGE=1,A_DISCARDING=2;
inline std::array<std::atomic<u8>, A_REGION/HP> A_MANAGED{};
inline void protect_managed(std::uintptr_t p,std::size_t n) noexcept {
    const auto off=p-A_BASE.load(relaxed),lo=off/HP,hi=(off+n-1)/HP;
    for (auto i=lo;i<=hi;++i) {
        auto state=A_MANAGED[i].load(std::memory_order_acquire);
        while (state!=A_MANAGED_PAGE) {
            if (state==A_UNCLAIMED && A_MANAGED[i].compare_exchange_weak(state,A_MANAGED_PAGE,
                  std::memory_order_acq_rel,std::memory_order_acquire)) break;
            state=A_MANAGED[i].load(std::memory_order_acquire);
        }
    }
}

inline void detect_pages() {
    std::ifstream f("/sys/kernel/mm/transparent_hugepage/enabled");
    std::string modes;
    std::getline(f,modes);
    SMALLPAGES.store(!HUGE_PAGES.load(relaxed) || modes.find("[never]") != std::string::npos,relaxed);
}
inline void advise_pages(void *p,std::size_t n,bool huge) {
#if defined(MADV_HUGEPAGE) && defined(MADV_NOHUGEPAGE)
    ::madvise(p,n,huge ? MADV_HUGEPAGE : MADV_NOHUGEPAGE);
#else
    (void)p; (void)n; (void)huge;
#endif
}
inline void release(const u8 *p,std::size_t bytes) {
    const auto a=(reinterpret_cast<std::uintptr_t>(p)+4095)&~std::uintptr_t{4095};
    const auto b=(reinterpret_cast<std::uintptr_t>(p)+bytes)&~std::uintptr_t{4095};
    if (b>a) ::madvise(reinterpret_cast<void*>(a),b-a,MADV_DONTNEED);
}
inline void release_heap_part(std::size_t tid,std::size_t nt) {
    const auto base=A_BASE.load(relaxed);
    if (!base) return;
    const auto used=(A_NEXT.load(relaxed)-base+HP-1)/HP;
    const auto lo=used*tid/nt,hi=used*(tid+1)/nt;
    auto discard=[&](std::size_t a,std::size_t b){
        if (a==b) return;
        release(reinterpret_cast<const u8*>(base+a*HP),(b-a)*HP);
        for (auto j=a;j<b;++j) A_MANAGED[j].store(A_UNCLAIMED,std::memory_order_release);
    };
    auto start=lo;
    for (auto i=lo;i<hi;++i) {
        u8 state=A_UNCLAIMED;
        if (A_MANAGED[i].compare_exchange_strong(state,A_DISCARDING,std::memory_order_acq_rel)) continue;
        discard(start,i);start=i+1;
    }
    discard(start,hi);
}
inline bool pin_to(std::size_t cpu) {
#if defined(__linux__)
    if (cpu>=CPU_SETSIZE) return false;
    cpu_set_t set; CPU_ZERO(&set); CPU_SET(cpu,&set);
    return sched_setaffinity(0,sizeof(set),&set)==0;
#else
    (void)cpu; return false;
#endif
}
struct ExecInfo {
    std::vector<std::string> args;
    std::vector<char*> ptrs;
    std::string data, out;
    std::size_t ncpu{};
    std::thread::id main;
    void (*generic)(const std::string&,const std::string&,std::size_t){};
};
inline ExecInfo exec_info;
inline void exec_init(int argc,char **argv,const std::string &data,const std::string &out,
                      std::size_t ncpu,decltype(ExecInfo::generic) generic=nullptr) {
    auto &e=exec_info;
    e.args={argc ? argv[0] : "pipeline","--generic"};
    if (argc>=3) {e.args.push_back(data); e.args.push_back(out);}
    e.ptrs.clear();
    for (auto &s:e.args) e.ptrs.push_back(s.data());
    e.ptrs.push_back(nullptr);
    e.data=data; e.out=out; e.ncpu=ncpu; e.generic=generic; e.main=std::this_thread::get_id();
}
[[noreturn]] inline void fallback(const char *why) {
    std::fprintf(stderr,"[pipeline] fallback: %s\n",why);
    auto &e=exec_info;
    if (!e.args.empty()) {
#if defined(__linux__)
        ::execv("/proc/self/exe",e.ptrs.data());
#endif
        ::execv(e.args[0].c_str(),e.ptrs.data());
        if (!WORKERS.load() && e.generic && std::this_thread::get_id()==e.main)
            e.generic(e.data,e.out,e.ncpu);
    }
    ::_exit(3);
}
inline constexpr int anonymous_flags=MAP_PRIVATE|MAP_ANONYMOUS
#ifdef MAP_NORESERVE
    |MAP_NORESERVE
#endif
;
inline std::size_t a_region(std::size_t size,std::size_t alignment) {
    auto cursor=A_NEXT.load(relaxed);
    const auto stop=A_END.load(relaxed);
    for (;;) {
        const auto a=(cursor+alignment-1)&~(alignment-1);
        if (a>stop || size>stop-a) return 0;
        if (A_NEXT.compare_exchange_weak(cursor,a+size,relaxed)) return a;
    }
}
inline void alloc_enable() {
    static std::atomic_flag installing{};
    if (A_BASE.load(std::memory_order_acquire) || installing.test_and_set(std::memory_order_acquire)) return;
    void *p=::mmap(nullptr,A_REGION+HP,PROT_READ|PROT_WRITE,anonymous_flags,-1,0);
    if (p==MAP_FAILED) {installing.clear(std::memory_order_release);return;}
    const auto a=(reinterpret_cast<std::uintptr_t>(p)+HP-1)&~(HP-1);
    advise_pages(reinterpret_cast<void*>(a),A_REGION,HUGE_PAGES.load(relaxed));
    A_NEXT.store(a); A_END.store(a+A_REGION); A_BASE.store(a);
}
inline u8 *anon(std::size_t n) {
    if (ANON_TINY.load(relaxed) && n<=4096 && A_BASE.load(relaxed)) {
        if (auto p=a_region(4096,4096)) return reinterpret_cast<u8*>(p);
    }
    if (n>SIZE_MAX-2*HP) fallback("mapping size overflow");
    n=(n+HP-1)&~(HP-1);
    if (ANON_REGION.load(relaxed) && A_BASE.load(relaxed)) {
        if (auto p=a_region(n,HP)) return reinterpret_cast<u8*>(p);
    }
    void *p=::mmap(nullptr,n+HP,PROT_READ|PROT_WRITE,anonymous_flags,-1,0);
    if (p==MAP_FAILED) fallback("mmap failed");
    auto *a=reinterpret_cast<u8*>((reinterpret_cast<std::uintptr_t>(p)+HP-1)&~(HP-1));
    advise_pages(a,n,HUGE_PAGES.load(relaxed));
    return a;
}
class Arena {
    u8 *base_{};
    std::size_t cap_{};
    mutable std::atomic<std::size_t> next_{};
public:
    Arena()=default;
    explicit Arena(std::size_t cap):base_(anon(cap)),cap_(cap){}
    Arena(Arena&& a) noexcept:base_(a.base_),cap_(a.cap_),next_(a.used()) {}
    Arena& operator=(Arena&& a) noexcept {
        base_=a.base_; cap_=a.cap_; next_.store(a.used(),relaxed); return *this;
    }
    u8 *alloc(std::size_t n) const {
        auto off=next_.fetch_add(n,relaxed);
        if (off>cap_ || n>cap_-off) fallback("arena full");
        return base_+off;
    }
    std::size_t used() const { return next_.load(relaxed); }
    std::pair<u8*,std::size_t> span() const { return {base_,cap_}; }
    void release_part(std::size_t tid,std::size_t nt) const {
        if (!RELEASE.load(relaxed)) return;
        auto used_pages=std::min(used(),cap_)/HP;
        auto a=used_pages*tid/nt*HP, b=used_pages*(tid+1)/nt*HP;
        if (b>a) ::madvise(base_+a,b-a,MADV_DONTNEED);
    }
};
template<class T> class AVec {
    static_assert(std::is_trivially_copyable_v<T>);
    T *p_{};
    std::size_t n_{},cap_{};
public:
    AVec()=default;
    explicit AVec(std::size_t capacity):cap_(std::max<std::size_t>(16,capacity)) {
        if (cap_>SIZE_MAX/sizeof(T)) fallback("avec size overflow");
        p_=reinterpret_cast<T*>(anon(cap_*sizeof(T)));
    }
    static AVec empty() {return {};}
    void push(T v) {
        if (n_==cap_) fallback("avec full");
        std::memcpy(p_+n_++,&v,sizeof(v));
    }
    void extend(std::span<const T> s) {
        if (s.size()>cap_-n_) fallback("avec full");
        if (!s.empty()) std::memcpy(p_+n_,s.data(),s.size_bytes());
        n_+=s.size();
    }
    std::span<const T> as_slice() const {return {p_,n_};}
    std::size_t len() const {return n_;}
    T *as_mut_ptr() {return p_;}
    const T *as_ptr() const {return p_;}
    void truncate(std::size_t n) {n_=n;}
    void clear() {n_=0;}
    void release() const {sys::release(reinterpret_cast<const u8*>(p_),n_*sizeof(T));}
};
class U64Map {
    std::vector<std::array<u64,2>> slots_;
    std::size_t mask_,len_{};
    static std::size_t h(u64 k,std::size_t mask) {return std::size_t((k*0x9e3779b97f4a7c15ULL)>>20)&mask;}
    void grow() {
        auto old=std::move(slots_);
        slots_.assign(old.size()*2,{}); mask_=slots_.size()-1;
        for (auto s:old) if (s[0]) {
            auto i=h(s[0]-1,mask_);
            while (slots_[i][0]) i=(i+1)&mask_;
            slots_[i]=s;
        }
    }
public:
    explicit U64Map(std::size_t n=0):
        slots_(std::max<std::size_t>(1024,std::bit_ceil(2*n))),mask_(slots_.size()-1) {}
    static U64Map with_capacity(std::size_t n) {return U64Map(n);}
    std::size_t cap() const {return slots_.size();}
    std::optional<u32> get(u64 k) const {
        const auto stored=k+1;
        for (auto i=h(k,mask_);;i=(i+1)&mask_) {
            auto s=slots_[i];
            if (s[0]==stored) return u32(s[1]);
            if (!s[0]) return {};
        }
    }
    u32 get_or_insert(u64 k,u32 value) {
        if (len_*2>=slots_.size()) grow();
        const auto stored=k+1;
        for (auto i=h(k,mask_);;i=(i+1)&mask_) {
            auto &s=slots_[i];
            if (s[0]==stored) return u32(s[1]);
            if (!s[0]) {s={stored,value}; ++len_; return value;}
        }
    }
};
}

#include "sys_alloc.hpp"
#include "sys_workers.hpp"
