#pragma once
#include "../iface.hpp"
#include <atomic>
#include <bit>
#include <cerrno>
#include <cstring>
#include <exception>
#include <filesystem>
#include <fcntl.h>
#include <limits>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <sys/mman.h>
#include <sys/stat.h>
#include <system_error>
#include <thread>
#include <unordered_map>
#include <unistd.h>
#include <utility>
#include <vector>
#if defined(__x86_64__)
#include <immintrin.h>
#endif

namespace pipeline::generic {
using B=std::string_view;
using i32=std::int32_t;
namespace fs=std::filesystem;
inline constexpr u64 K0=0x243f6a8885a308d3ULL,K1=0x13198a2e03707344ULL,K2=0xa4093822299f31d0ULL,
                     K3=0x082efa98ec4e6c89ULL,K4=0x452821e638d01377ULL;
inline u64 mix(u64 a,u64 b) {
    auto x=static_cast<__uint128_t>(a)*b;
    return u64(x)^u64(x>>64);
}
template<class T> inline T little(const char *p) {
    T x;
    std::memcpy(&x,p,sizeof(x));
    if constexpr (std::endian::native==std::endian::big) {
        if constexpr (sizeof(x)==8) return __builtin_bswap64(x);
        else return __builtin_bswap32(x);
    }
    return x;
}
inline u64 hash(B b) {
    const auto n=b.size();
    const auto p=b.data();
    if (n<=16) {
        u64 a=0,c=0;
        if (n>=8) {a=little<u64>(p);c=little<u64>(p+n-8);}
        else if (n>=4) {a=little<u32>(p);c=little<u32>(p+n-4);}
        else if (n) a=(u64(u8(p[0]))<<16)|(u64(u8(p[n/2]))<<8)|u8(p[n-1]);
        return mix(mix(a^K0,c^K1^n)^K2,K3);
    }
    u64 h=K4^n;
    for (std::size_t i=0;i+16<n;i+=16) h=mix(little<u64>(p+i)^K0,little<u64>(p+i+8)^h);
    h=mix(little<u64>(p+n-16)^K1,little<u64>(p+n-8)^h^K2);
    return mix(h^K2,K3);
}
struct H {
    std::size_t operator()(u64 x) const noexcept {return mix(x^K0,K3);}
    std::size_t operator()(B b) const noexcept {return mix(mix(b.size()^K0,K3)^hash(b),K3);}
};
template<class K,class V> using Map=std::unordered_map<K,V,H>;

class Tab {
    std::vector<u64> slots;
    std::size_t mask{};
public:
    Tab()=default;
    explicit Tab(std::size_t n) {reset(n);}
    void reset(std::size_t n) {
        if (n>std::size_t{1}<<(std::numeric_limits<std::size_t>::digits-2)) throw std::length_error("hash slot count");
        slots.assign(std::bit_ceil(std::max<std::size_t>(16,n*2)),0);
        mask=slots.size()-1;
    }
    struct Match {std::size_t slot;std::optional<u32> index;};
    template<class Eq> Match find(u64 h,Eq equal) const {
        for (auto i=std::size_t(h)&mask;;i=(i+1)&mask) {
            u64 s=slots[i];
            if (!s) return {i,{}};
            if (s>>32==h>>32 && equal(u32(s)-1)) return {i,u32(s)-1};
        }
    }
    void set(std::size_t slot,u64 h,u32 i) {slots[slot]=(h&0xffffffff00000000ULL)|(u64(i)+1);}
};

inline u64 eq_mask_scalar(const char *p,char c) {
    u64 m=0;
    for(unsigned i=0;i<64;++i) m|=u64(p[i]==c)<<i;
    return m;
}
#if defined(__x86_64__) && (defined(__GNUC__) || defined(__clang__))
[[gnu::target("avx512f,avx512bw")]] inline u64 eq_mask_wide(const char *p,char c) {
    return _mm512_cmpeq_epi8_mask(_mm512_loadu_si512(p),_mm512_set1_epi8(c));
}
#endif
inline u64 eq_mask64(const char *p,char c) {
#if defined(__AVX512BW__)
    return eq_mask_wide(p,c);
#elif defined(__x86_64__) && (defined(__GNUC__) || defined(__clang__))
    static const bool wide=__builtin_cpu_supports("avx512f") && __builtin_cpu_supports("avx512bw");
    if (wide) return eq_mask_wide(p,c);
#endif
    return eq_mask_scalar(p,c);
}
inline std::size_t find_byte(B b,char c) {
    std::size_t i=0;
    for (;i+64<=b.size();i+=64) {
        auto m=eq_mask64(b.data()+i,c);
        if(m) return i+std::countr_zero(m);
    }
    auto r=b.find(c,i);
    return r;
}
inline std::size_t count_byte(B b,char c) {
    std::size_t i=0,n=0;
    for (;i+64<=b.size();i+=64) n+=std::popcount(eq_mask64(b.data()+i,c));
    for (;i<b.size();++i) n+=b[i]==c;
    return n;
}

class Mapped {
    void *ptr_{};
    std::size_t len_{};
public:
    explicit Mapped(const fs::path &path) {
        int fd=::open(path.c_str(),O_RDONLY);
        if (fd<0) throw std::system_error(errno,std::generic_category(),path.string());
        struct stat st{};
        if (!::fstat(fd,&st)) len_=std::size_t(st.st_size);
        if (len_) ptr_=::mmap(nullptr,len_,PROT_READ,MAP_PRIVATE,fd,0);
        int error=errno;
        ::close(fd);
        if (ptr_==MAP_FAILED) throw std::system_error(error,std::generic_category(),path.string());
    }
    Mapped(const Mapped&)=delete;
    Mapped(Mapped &&other) noexcept:ptr_(std::exchange(other.ptr_,nullptr)),len_(std::exchange(other.len_,0)) {}
    ~Mapped() {if(len_) ::munmap(ptr_,len_);}
    B bytes() const {return len_?B(static_cast<const char *>(ptr_),len_):B{};}
};
inline std::vector<Mapped> map_dir(const fs::path &dir) {
    std::vector<fs::path> paths;
    std::error_code ec;
    for (fs::directory_iterator it(dir,ec),end;!ec && it!=end;it.increment(ec)) {
        std::error_code status;
        if (it->is_regular_file(status) && it->path().filename().string().ends_with(".csv")) paths.push_back(it->path());
    }
    std::sort(paths.begin(),paths.end());
    std::vector<Mapped> mappings;
    mappings.reserve(paths.size());
    for(auto &p:paths) mappings.emplace_back(p);
    return mappings;
}

template<class Init,class F> auto par_run(std::size_t n,std::size_t threads,Init init,F f) {
    using State=decltype(init());
    using T=decltype(f(std::declval<State &>(),std::size_t{}));
    threads=std::clamp<std::size_t>(threads,1,std::max<std::size_t>(n,1));
    std::atomic_size_t next{};
    std::vector<std::optional<State>> states(threads);
    std::vector<std::optional<T>> result(n);
    std::vector<std::exception_ptr> errors(threads);
    auto worker=[&](std::size_t tid) {
        try {
            auto &s=states[tid].emplace(init());
            for(;;) {
                auto i=next.fetch_add(1,std::memory_order_relaxed);
                if(i>=n) break;
                result[i].emplace(f(s,i));
            }
        } catch (...) {
            errors[tid]=std::current_exception();
            next.store(n,std::memory_order_relaxed);
        }
    };
    if (threads==1) worker(0);
    else {
        std::vector<std::jthread> jobs;
        jobs.reserve(threads);
        for(std::size_t t=0;t<threads;++t) jobs.emplace_back(worker,t);
    }
    for(auto &e:errors) if(e) std::rethrow_exception(e);
    std::vector<State> done;
    done.reserve(threads);
    for(auto &s:states) done.push_back(std::move(*s));
    std::vector<T> out;
    out.reserve(n);
    for(auto &r:result) out.push_back(std::move(*r));
    return std::pair{std::move(out),std::move(done)};
}
}
