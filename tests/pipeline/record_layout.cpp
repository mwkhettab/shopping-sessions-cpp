#include "../../cpp/pipeline/model_types.hpp"
#include <cassert>
#include <charconv>
#include <random>
int main() {
    using namespace pipeline;
    std::mt19937_64 rng(94172);
    for (unsigned i=0;i<2000000;++i) {
        u64 n=i<100000 ? i : rng()%2821109907456ULL >> (i%41);
        auto [word,len]=enc36(n);
        char digits[20];
        auto [end,ec]=std::to_chars(digits,digits+20,n,36);
        assert(ec==std::errc{} && len==std::size_t(end-digits));
        for (std::size_t j=0;j<len;++j) assert(u8(word>>(56-j*8))==u8(digits[j]));
        if (len<8) assert(u8(word>>(56-len*8))==0);
        assert(shard(n)<NSHARD);
    }
}
