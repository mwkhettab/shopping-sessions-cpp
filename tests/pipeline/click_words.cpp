#include "links_part.hpp"
#include "model.hpp"
#include <cassert>
#include <cstdio>

using namespace pipeline;
int main() {
    std::array<u16,8> users{0,COMPLEX,COMPLEX,0,0,COMPLEX,0,0};
    std::array<u32,9> offsets{0,0,2,2,5,6,10,10,12};
    const auto original=offsets;
    std::vector<links::Click> records(offsets.back());
    for(u32 i=0;i<records.size();++i) records[i]={u64(i/2)<<32|i,0x123400+i};
    constexpr auto guard=UINT64_MAX;
    std::array<links::Click,24> arena;
    for(auto& c:arena) c={guard,guard};
    auto* words=reinterpret_cast<u64*>(arena.data());
    links::store_click_words(users,offsets,records,words,3);
    u8 tiled=1;
    G g;g.csr=arena.data();g.csr_off=offsets.data();g.utab=users.data();g.word_bucket=&tiled;
    for(u32 u=0;u<users.size();++u) {
        const auto expected=std::span(records).subspan(original[u],original[u+1]-original[u]);
        if(users[u]&COMPLEX) {
            assert(!g.simple_words(u));
            const auto got=g.csr_range(u);
            assert(std::equal(got.begin(),got.end(),expected.begin(),expected.end()));
        } else {
            assert(g.simple_words(u));
            auto got=g.click_words(u);assert(got.size()==expected.size());
            for(std::size_t k=0;k<got.size();++k) assert(got[k]==expected[k][0]);
        }
    }
    assert(words[2]==guard && words[offsets.back()]==guard);
    std::vector<u32> counts(2*UBSIZE);links::ScratchVec<u64> links;
    std::array<Rec,3> raw{{{0,0},{RAW,0},{u64{1}<<63,0}}};
    assert(!(pipeline::links::count_chunk(std::span(raw).first(1),counts,links)&RAW));
    assert(pipeline::links::count_chunk(std::span(raw).subspan(1),counts,links)&RAW);
    assert(counts[0]==2 && counts[UBSIZE]==1 && links.size()==1);
    std::puts("Checked compact keys, full click IDs, empty ranges and opaque ID detection.");
}
