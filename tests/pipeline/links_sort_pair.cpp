#ifdef NDEBUG
#undef NDEBUG
#endif
#include <algorithm>
#include <cassert>
#include <cstdio>
#include <cstring>
#include <random>
#include <sys/mman.h>
#include <unistd.h>

#if defined(SHOP_TEST_LINKS_PAIR_SIMDE)
#include "routing_simde.hpp"
// SIMDe 0.8.2 lacks these AVX512VL spellings. Masked memory accesses visit
// active lanes only, including in the protected-page tests below.
template<class T> __m256i test_load256(unsigned mask,const void* p) {
    T v[32/sizeof(T)]{};
    for(std::size_t i=0;i<std::size(v);++i) if(mask&(1u<<i))
        std::memcpy(&v[i],static_cast<const char*>(p)+i*sizeof(T),sizeof(T));
    return _mm256_loadu_si256(reinterpret_cast<const __m256i*>(v));
}
inline void test_store256(void* p,unsigned mask,__m256i v) {
    auto x=simde__m256i_to_private(v);
    for(unsigned i=0;i<4;++i) if(mask&(1u<<i)) {
        const uint64_t lane=x.u64[i];
        std::memcpy(static_cast<char*>(p)+8*i,&lane,8);
    }
}
inline __m256i test_max64(__m256i a,__m256i b) {
    auto x=simde__m256i_to_private(a),y=simde__m256i_to_private(b);
    for(unsigned i=0;i<4;++i) x.u64[i]=std::max(uint64_t(x.u64[i]),uint64_t(y.u64[i]));
    return simde__m256i_from_private(x);
}
inline unsigned test_bits(__m256i a,__m256i b,bool nonzero) {
    const auto x=simde__m256i_to_private(a),y=simde__m256i_to_private(b);
    unsigned mask=0;
    for(unsigned i=0;i<4;++i) mask|=unsigned(bool(x.u64[i]&y.u64[i])==nonzero)<<i;
    return mask;
}
inline __m256i test_alignr3(__m256i a,__m256i b) {
    auto x=simde__m256i_to_private(a),y=simde__m256i_to_private(b);
    return _mm256_setr_epi64x(y.i64[3],x.i64[0],x.i64[1],x.i64[2]);
}
#define _mm256_maskz_loadu_epi64(m,p) test_load256<uint64_t>(m,p)
#define _mm256_maskz_loadu_epi32(m,p) test_load256<uint32_t>(m,p)
#define _mm256_mask_storeu_epi64 test_store256
#define _mm256_min_epu64 session_mm256_min_epu64
#define _mm256_max_epu64 test_max64
#define _mm256_test_epi64_mask(a,b) test_bits(a,b,true)
#define _mm256_testn_epi64_mask(a,b) test_bits(a,b,false)
#define _mm256_mask_or_epi64(s,m,a,b) _mm256_mask_mov_epi64(s,m,_mm256_or_si256(a,b))
#define _mm256_alignr_epi64(a,b,k) test_alignr3(a,b)
#define _mm256_cmpeq_epi32_mask(a,b) _mm256_movemask_ps(_mm256_castsi256_ps(_mm256_cmpeq_epi32(a,b)))
#endif
#include "links_sort_pair.hpp"

#if defined(SHOP_TEST_LINKS_PAIR_SIMDE) || defined(__x86_64__)
using namespace pipeline;
using links::Click;
struct Model {
    u64 clen=310000000;
    Click* csr{};
    void id_str(u64 tok,u8 role,std::vector<u8>& result) const {
        result={role};
        if(tok&RAW) {
            result.push_back('-');
            auto raw=std::to_string(tok&(RAW-1));
            result.insert(result.end(),raw.begin(),raw.end());
        } else {
            auto [v,n]=enc36(tok);
            for(unsigned i=0;i<n;++i) result.push_back(v>>(56-8*i));
        }
    }
};
struct Page {
    const std::size_t size=std::size_t(sysconf(_SC_PAGESIZE));
    u8* const ptr=static_cast<u8*>(mmap(nullptr,2*size,PROT_READ|PROT_WRITE,MAP_PRIVATE|MAP_ANONYMOUS,-1,0));
    Page() {assert(ptr!=MAP_FAILED);assert(mprotect(ptr+size,size,PROT_NONE)==0);}
    ~Page() {munmap(ptr,2*size);}
    template<class T> T* tail(std::size_t n) {
        assert(n<=size/sizeof(T));
        auto* p=reinterpret_cast<T*>(ptr+size-sizeof(T)*n);
        std::uninitialized_value_construct_n(p,n);
        return p;
    }
};
Click make_click(std::mt19937_64& r,unsigned rep,std::size_t i) {
    const u64 ts=rep%4==0 ? 1 : rep%4==1 ? r()%7 : r()%4000000000ULL;
    const u64 camp=rep%3==0 ? 7 : r()%2000;
    const u64 token=rep%5==0 ? r()%3 : r()%((u64{1}<<39)-1);
    const u64 reserved=rep%7==0 && i%5==0 ? u64{1}<<(18+r()%14) : 0;
    return {(ts<<32)|(camp<<4)|(r()%14)|reserved,token|(rep%3==1 && i%5==1 ? RAW:0)};
}
void check_networks(Model& g,std::mt19937_64& r) {
    Page in_page,out_page;
    for(unsigned n=0;n<=128;++n) {
        auto* src=in_page.tail<Click>(n);auto* dest=out_page.tail<Click>(n);
        for(unsigned rep=0;rep<16;++rep) {
            out_page.ptr[out_page.size-n*sizeof(Click)-1]=0x5b;
            for(unsigned i=0;i<n;++i) src[i]=make_click(r,rep,i);
            std::vector<Click> oracle(src,src+n);
            links::slow_from(g,oracle.data(),std::span(oracle));
            std::span<Click> out(dest,n);
            links::pair::small(g,src,out);
            assert(std::ranges::equal(out,oracle));
            assert(out_page.ptr[out_page.size-n*sizeof(Click)-1]==0x5b);
            std::copy_n(src,n,dest);
            links::pair::small(g,dest,out);
            assert(std::ranges::equal(out,oracle));
#if defined(__x86_64__)
            if(n>=3) {
                if(n<=8) links::sort_net<1>(g,src,out);
                else if(n<=16) links::sort_net<2>(g,src,out);
                else if(n<=32) links::sort_net<4>(g,src,out);
                else if(n<=64) links::sort_net<8>(g,src,out);
                else links::sort_net<16>(g,src,out);
                assert(std::ranges::equal(out,oracle));
            }
#endif
        }
    }
}
void check_users(Model& g,std::mt19937_64& r) {
    constexpr unsigned users=192;
    Page page;
    auto* off=page.tail<u32>(users+1);off[0]=0;
    std::vector<Click> data;
    for(unsigned u=0;u<users;++u) {
        constexpr unsigned sizes[]{0,1,2,3,7,8,9,15,16,17,32,64,65,127,128,129,2047,2048,2049};
        const auto n=sizes[r()%std::size(sizes)];
        for(unsigned i=0;i<n;++i) data.push_back(make_click(r,u,i));
        off[u+1]=u32(data.size());
    }
    links::Scratch sc;
    for(unsigned a=0;a<=users;++a) {
        const auto b=(a%5==0)?users:std::min(users,a+a%32);
        links::pair::classify(off,a,b,sc.lens);
        std::array<std::vector<u32>,4> expected;
        for(auto u=a;u<b;++u) {
            const auto n=off[u+1]-off[u];
            if(n>=2) expected[n==2?0:n<=8?1:n<=16?2:3].push_back(u);
        }
        for(unsigned i=0;i<4;++i) assert(std::ranges::equal(sc.lens[i],expected[i]));
    }
    auto expected=data;
    for(unsigned u=0;u<users;++u) {
        auto s=std::span(expected).subspan(off[u],off[u+1]-off[u]);
        links::slow_from(g,s.data(),s);
    }
    g.csr=data.data();
    for(unsigned a=0;a<users;a+=11) links::pair::users(g,off,a,std::min(users,a+11),sc);
    assert(data==expected);
}
#if defined(__x86_64__)
PIPELINE_ROUTING_X64
#endif
void check_masks() {
    for(unsigned a=0;a<256;++a) for(unsigned b=0;b<256;++b) {
        assert(links::pair::mask32(u8(a),u8(b))==(a|(b<<8)));
        assert(links::pair::mask64(u8(a&15),u8(b&15))==((a&15)|((b&15)<<4)));
    }
}
int main() {
#if defined(__x86_64__)
    if(!parse::routing_x64()) {std::puts("paired click test requires AVX512VL");return 77;}
#endif
    Model g;std::mt19937_64 r(0xee42f48);
    check_masks();check_networks(g,r);check_users(g,r);
    std::puts("paired click masks, guarded 0..128-row networks, and classified user groups match the oracle");
}
#else
int main() {std::puts("paired click test requires AVX512VL or SHOP_TEST_LINKS_PAIR_SIMDE");return 77;}
#endif
