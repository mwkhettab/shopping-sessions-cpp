#include "links_sort.hpp"
#include <array>
#include <cassert>
#include <cstdint>
#include <string>
#include <vector>

using namespace pipeline;
struct Model {
    links::Click* csr;
    u64* click_keys;
    void id_str(u64 token, u8, std::vector<u8>& out) const {
        const auto label = std::to_string(token ^ RAW);
        out.assign(label.begin(), label.end());
    }
};
int main() {
    constexpr unsigned users = 16;
    std::array<u32,users+1> offsets{};
    std::vector<links::Click> rows;
    for (unsigned u=0;u<users;++u) {
        offsets[u] = rows.size();
        for (unsigned k=0;k<u+2;++k)
            rows.push_back({(u64(100+k%2)<<32)|(u64((u+k)%5)<<4),RAW|(1000+37u*k)});
    }
    offsets.back() = rows.size();
    constexpr u64 guard = UINT64_MAX;
    std::vector<u64> keys(rows.size()+2,guard);
    Model g{rows.data(),keys.data()+1};
    links::Scratch scratch;
    links::DEFER_RAW.store(true);
    links::sort_users(g,offsets.data(),0,7,scratch);
    links::sort_users(g,offsets.data(),7,users,scratch);
    assert(!links::deferred.empty());
    for (unsigned i=0;i<rows.size();++i) assert(g.click_keys[i]==rows[i][0]);
    links::DEFER_RAW.store(false);
    auto before=keys;
    for(auto group:links::deferred) {
        links::raw_ties(g,group);
        const auto lo=std::size_t(group.data()-g.csr);
        links::copy_click_keys(g,lo,lo+group.size());
    }
    assert(keys.front()==guard && keys.back()==guard && keys!=before);
    for (unsigned i=0;i<rows.size();++i) assert(g.click_keys[i]==rows[i][0]);
}
