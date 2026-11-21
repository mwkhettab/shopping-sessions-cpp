#pragma once
#include "links_uf.hpp"
#include "parse_part.hpp"
#include <memory>
#include <tuple>
#include <unordered_map>

namespace pipeline {
struct TState {
    parse::Part ev,us;
    std::vector<std::pair<u64,u32>> raws;
    std::vector<u8> rawbuf;
    std::vector<u64> odd_users;
    std::vector<OddEv> odd_events;
    std::vector<std::array<u64,3>> odd_clicks;
    std::vector<std::pair<u64,u64>> gen_edges;
    std::vector<std::tuple<u32,u8,u8>> cx_edges;
    std::vector<u32> cx_users,mixed_users;
    std::vector<Rec> slots;
    sys::AVec<Rec> edge;
    std::vector<Rec> cx_raw;
    std::vector<std::vector<Rec>> cx_ev{NSHARD};
    std::vector<std::vector<std::array<u64,3>>> cx_ck{NSHARD};
    std::vector<std::array<u64,2>> ck;
    std::vector<Rec> runbuf;
    std::vector<std::pair<u32,u16>> pend;
    std::vector<u32> cnt;
    core::Core core;
    u64 n_ev_rows{},n_ck_rows{},n_lk_rows{},n_simple{},n_complex{},n_odd{},n_runs{};
    std::array<u64,8> prof{};
};

struct G {
    std::size_t nt{};
    bool check{};
    i64 ws{};
    u64 wlen{};
    i64 cbase{};
    u64 clen{};
    i64 ing_base{};
    u64 ws_mod{};
    i32 day0{};
    std::size_t nd{};
    u64 ukey{},ekey{},ckey{},ccap{};
    bool cpart{};
    u64 ecap{},ucap{};
    std::vector<u8> prod,camp;
    std::size_t nc{};
    std::vector<std::vector<u8>> cats,channels;
    const u8 *region{};
    std::vector<FileInfo> files;
    std::vector<Chunk> queue;
    mutable std::atomic<std::size_t> qnext{};
    std::size_t ev_bytes{},row_cap{},nb{},nub{};
    sys::Arena ev_arena,us_arena;
    std::vector<std::unique_ptr<TState>> workers;
    u16 *utab{};
    std::atomic<u64> *claim{};
    u32 *csr_off{};
    std::array<u64,2> *csr{};
    std::size_t csr_cap{};
    std::vector<std::size_t> csr_base;
    std::size_t ncsr{};
    links::Uf uf;
    EdgeInfo *edges{};
    std::vector<std::vector<Seg>> joined;
    std::vector<u32> cxu;
    std::unordered_map<u32,std::vector<u64>> extra;
    std::vector<OddEv> odd_kept;
    mutable std::atomic<std::size_t> next{};
    std::atomic_bool st2b_done{};

    TState& t(std::size_t tid) const { return *workers[tid]; }
    std::optional<std::pair<u32,u32>> normal(u64 y) const {
        if (y < (u64{1} << 40)) {
            const auto w=keyrec::unscramble(y,ukey),u=w>>9,a=w&511;
            if (u<ucap && a<NALIAS) return std::pair{u32(u),u32(a)};
        }
        return {};
    }
    std::span<const u8> raw_bytes(u64 tok) const {
        const auto& worker=t((tok>>34)&15);
        const auto [off,len]=worker.raws[tok&((u64{1}<<34)-1)];
        return std::span<const u8>(worker.rawbuf).subspan(off,len);
    }
    void id_str(u64 tok,u8 prefix,std::vector<u8>& out) const {
        out.clear();
        if (tok&RAW) {
            const auto b=raw_bytes(tok);
            out.insert(out.end(),b.begin(),b.end());
        } else {
            const auto [word,n]=enc36(tok);
            const u64 le=__builtin_bswap64(word);
            const auto *p=reinterpret_cast<const u8*>(&le);
            out.push_back(prefix);
            out.insert(out.end(),p,p+n);
        }
    }
    std::span<const std::array<u64,2>> csr_range(u32 u) const {
        const std::size_t i=std::size_t(u)+(u>>UB);
        const auto a=csr_off[i],b=csr_off[i+1];
        return {csr+a,b-a};
    }
};
}
