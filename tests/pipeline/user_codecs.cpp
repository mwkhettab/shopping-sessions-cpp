#include "pqw_user_encode.hpp"
#include <algorithm>
#include <cstdio>
#include <stdexcept>

using namespace pipeline;

static void check(bool ok,const char* reason) {
    if(!ok) throw std::runtime_error(reason);
}

int main() {
    u32 random=638291;
    auto next=[&]{random=random*1664525+1013904223;return random;};
    const std::array<unsigned,24> lengths{0,1,2,7,8,9,15,16,17,31,32,33,62,63,64,65,
        125,126,127,255,256,257,511,1093};
    for(unsigned w=1;w<=32;++w) for(const auto n:lengths) {
        const u32 mask=UINT32_MAX>>(32-w),base=next();
        std::vector<u32> keys(n);
        for(auto& k:keys) k=base+(next()&mask);
        pqw::Out got,expected;
        pqw::emit_user_words(got,keys,base,w);
        pqw::emit_packed(expected,n,w,[&](auto i){return keys[i]-base;});
        check(got.size()==expected.size(),"word page length");
        if(got.size()) check(!std::memcmp(got.data(),expected.data(),got.size()),"word page payload");
        for(unsigned period:{1u,2u,8u,63u,64u,127u,513u}) {
            for(unsigned i=0;i<n;++i) if(i%period) keys[i]=keys[i-1];
            const auto rb=1+(w+7)/8;
            const auto cap=n*rb;
            std::vector<u8> runs(cap+64,0xcd);
            const auto size=pqw::encode_user_runs(runs.data(),keys,base,rb);
            check(size<=cap && size%rb==0,"run length");
            check(std::all_of(runs.begin()+cap+32,runs.end(),[](u8 b){return b==0xcd;}),"run guard");
            std::size_t at=0;
            for(std::size_t i=0;i<size;i+=rb) {
                check(runs[i] && !(runs[i]&1) && runs[i]<=126,"run header");
                u32 value=0;
                for(unsigned j=1;j<rb;++j) value|=u32(runs[i+j])<<(8*(j-1));
                for(unsigned j=0;j<runs[i]/2;++j) {
                    check(at<n && keys[at]==base+value,"run payload");++at;
                }
            }
            check(at==n,"run row count");
            for(unsigned i=0;i<n;++i) keys[i]=base+(next()&mask);
        }
    }
#if defined(__x86_64__) && (defined(__clang__) || defined(__GNUC__))
    if(__builtin_cpu_supports("avx2") && __builtin_cpu_supports("bmi2") && __builtin_cpu_supports("popcnt")) {
        for(unsigned w=1;w<=6;++w) for(auto n:lengths) for(unsigned density:{0u,1u,3u,8u}) {
            std::vector<u8> cat(n),levels((n+7)/8+16,0xcd),packed(n+32,0xcd);
            std::vector<u8> expected_levels((n+7)/8);
            std::vector<u32> values;
            for(unsigned i=0;i<n;++i) {
                const auto bits=next();
                cat[i]=(bits>>8)%8<density?u8(bits&((1u<<w)-1)):UINT8_MAX;
                if(cat[i]!=UINT8_MAX) {expected_levels[i/8]|=u8(1u<<(i%8));values.push_back(cat[i]);}
            }
            pqw::pack_user_categories(levels.data(),packed.data(),cat,w);
            check(std::equal(expected_levels.begin(),expected_levels.end(),levels.begin()),"definition levels");
            check(levels[expected_levels.size()]==0xcd,"definition level guard");
            const auto bytes=(values.size()+7)/8*w;
            std::vector<u8> expected(bytes+16);
            pqw::Packer p(expected.data(),w);
            for(auto v:values) p.push(v);
            p.finish(bytes);
            check(std::equal(expected.begin(),expected.begin()+bytes,packed.begin()),"category codes");
            check(packed[bytes+8]==0xcd,"category code guard");
        }
    }
    std::printf("AVX2=%d BMI2=%d AVX512VL=%d\n",bool(__builtin_cpu_supports("avx2")),
                bool(__builtin_cpu_supports("bmi2")),bool(__builtin_cpu_supports("avx512vl")));
#endif
    std::puts("Passed all dictionary bit widths, record sizes and run boundaries.");
}
