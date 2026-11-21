#include "links_uf.hpp"
#include <cassert>
#include <cstdio>
#include <string>
#include <tuple>
using namespace pipeline;
struct State {
    std::vector<std::pair<u64,u64>> gen_edges;
    std::vector<u64> odd_users;
    std::vector<std::array<u64,3>> odd_clicks;
    std::vector<std::tuple<u32,u8,u8>> cx_edges;
};
struct Model {
    std::array<State,2> st{};
    State& t(std::size_t i) {return st[i];}
    const State& t(std::size_t i) const {return st[i];}
    std::size_t nt=2,ccap=17;
    u64 ukey=43409;
    std::array<u16,8> utab{};
    std::array<std::string,3> raw{"u001","u001","u-odd"};
    std::span<const u8> raw_bytes(u64 tok) const {
        const auto& s=raw[tok&(RAW-1)];return {reinterpret_cast<const u8*>(s.data()),s.size()};
    }
    void id_str(u64 tok,u8 prefix,std::vector<u8>& v) const {
        v.clear();
        if(tok&RAW) {auto b=raw_bytes(tok); v.assign(b.begin(),b.end());}
        else {const auto [w,n]=enc36(tok);v.push_back(prefix);for(std::size_t i=0;i<n;++i) v.push_back(u8(w>>(56-i*8)));}
    }
    std::optional<std::pair<u32,u32>> normal(u64 tok) const {
        auto y=keyrec::unscramble(tok,ukey);
        if((y>>9)<utab.size() && (y&511)<NALIAS) return std::pair{u32(y>>9),u32(y&511)};
        return {};
    }
    links::Uf uf;
};
int main() {
    Model g;
    auto id=[&](u32 u,u32 a){return keyrec::scramble((u64(u)<<9)|a,g.ukey);};
    g.st[0].gen_edges={{RAW,1},{RAW+2,36},{1,RAW+2}};
    g.st[1].gen_edges={{id(5,2),id(4,1)}};
    g.st[1].cx_edges={{4,1,3},{4,3,9}};
    g.st[0].odd_users={RAW+1};g.st[0].odd_clicks={{id(2,0),9,4}};
    g.uf.part1(g);g.uf.part2a(g);g.uf.finish(g);
    assert(g.uf.len()==9);
    std::vector<u8> name;
    auto raw=links::resolve_token(g,RAW+1);
    assert(raw==links::resolve_token(g,36) && raw==links::resolve_token(g,RAW));
    links::name_of(g,raw,name);assert(std::string(name.begin(),name.end())=="u-odd");
    g.utab[4]=COMPLEX;g.utab[5]=COMPLEX;
    auto complex=links::resolve_normal(g,4,9);
    assert(complex==links::resolve_normal(g,5,2) && complex==links::resolve_normal(g,4,3));
    assert(links::resolve_normal(g,4,0)==64);
    g.utab[7]=6;
    assert(links::resolve_normal(g,7,1)==127 && links::resolve_normal(g,7,2)==127);
    assert(links::resolve_normal(g,7,3)==115);
    links::name_of(g,links::resolve_token(g,1295),name);
    assert(std::string(name.begin(),name.end())=="uzz");
    std::puts("identity graph, raw interning and three classes of person keys passed");
}
