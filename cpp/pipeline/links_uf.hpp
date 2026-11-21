#pragma once
#include "core.hpp"
#include "sys.hpp"
#include <algorithm>
#include <span>
#include <vector>

namespace pipeline::links {
inline constexpr u64 RAWKEY = u64{1} << 62;
inline u64 hbytes(std::span<const u8> bytes) {
    u64 h=0xcbf29ce484222325ULL;
    for (u8 c:bytes) h=(h^c)*0x100000001b3ULL;
    h=(h^(h>>29))*0xbf58476d1ce4e5b9ULL;
    return h^(h>>32);
}
struct Uf {
    sys::U64Map map;
    std::vector<u32> parent;
    std::vector<u64> toks;
    std::vector<u32> root;
    std::vector<std::pair<u32,u32>> name_at;
    std::vector<u8> name_buf;
    explicit Uf(std::size_t n=0):map(n) {parent.reserve(n); toks.reserve(n);}
    static Uf with_capacity(std::size_t n) {return Uf(n);}
    std::size_t len() const {return parent.size();}
    template<class G> u32 node(const G& g,u64 tok) {
        const u32 n=static_cast<u32>(parent.size());
        u32 got;
        if (!(tok&RAW)) got=map.get_or_insert(tok,n);
        else {
            const auto b=g.raw_bytes(tok);
            got=map.get_or_insert(RAWKEY|(hbytes(b)&(RAWKEY-1)),n);
            if (got!=n && !std::ranges::equal(g.raw_bytes(toks[got]),b))
                sys::fallback("raw ID hash collision");
        }
        if (got==n) {parent.push_back(n); toks.push_back(tok);}
        return got;
    }
    u32 find(u32 x) {
        u32 r=x;
        while (parent[r]!=r) r=parent[r];
        while (parent[x]!=r) {const u32 next=parent[x]; parent[x]=r; x=next;}
        return r;
    }
    void join(u32 a,u32 b) {
        const u32 ra=find(a),rb=find(b);
        if (ra!=rb) parent[std::max(ra,rb)]=std::min(ra,rb);
    }
    template<class G> void finish(const G& g) {
        const auto n=parent.size();
        root.resize(n);
        for (std::size_t i=0;i<n;++i) root[i]=find(static_cast<u32>(i));
        std::vector<u32> best(n,UINT32_MAX);
        std::vector<u64> enc; enc.reserve(n);
        for (auto t:toks) enc.push_back(t&RAW ? 0 : enc36(t).first);
        std::vector<u8> sa,sb;
        for (std::size_t i=0;i<n;++i) {
            auto& b=best[root[i]];
            bool smaller=b==UINT32_MAX;
            if (!smaller) {
                if (!((toks[i]|toks[b])&RAW)) smaller=enc[i]<enc[b];
                else {g.id_str(toks[i],'u',sa); g.id_str(toks[b],'u',sb); smaller=sa<sb;}
            }
            if (smaller) b=static_cast<u32>(i);
        }
        name_at.assign(n,{}); name_buf.clear();
        for (std::size_t r=0;r<n;++r) if (best[r]!=UINT32_MAX) {
            g.id_str(toks[best[r]],'u',sa);
            if (sa.size()>UINT32_MAX-name_buf.size()) sys::fallback("names too long");
            name_at[r]={static_cast<u32>(name_buf.size()),static_cast<u32>(sa.size())};
            name_buf.insert(name_buf.end(),sa.begin(),sa.end());
        }
    }
    std::optional<u32> canon_root(u64 y) const {
        if (auto n=map.get(y)) return root[*n];
        return {};
    }
    template<class G> std::optional<u32> raw_root(const G& g,std::span<const u8> b) const {
        const auto n=map.get(RAWKEY|(hbytes(b)&(RAWKEY-1)));
        if (!n || !std::ranges::equal(g.raw_bytes(toks[*n]),b)) return {};
        return root[*n];
    }
    std::span<const u8> name(u32 r) const {
        const auto [a,n]=name_at[r];
        return std::span(name_buf).subspan(a,n);
    }
    template<class G> void part1(const G& g) {
        for (std::size_t tid=0;tid<g.nt;++tid) {
            const auto& t=g.t(tid);
            for (const auto& [a,b]:t.gen_edges) {
                const auto na=node(g,a),nb=node(g,b); join(na,nb);
            }
            for (u64 t:t.odd_users) if (t&RAW) node(g,t);
            for (auto& c:t.odd_clicks) if ((c[0]&RAW) || (g.ccap && g.normal(c[0]))) node(g,c[0]);
        }
    }
    template<class G,class Edges> void cx(const G& g,const Edges& edges) {
        for (const auto& [u,a,b]:edges) {
            const u64 x=u64(u)<<9;
            const auto na=node(g,keyrec::scramble(x|a,g.ukey));
            const auto nb=node(g,keyrec::scramble(x|b,g.ukey)); join(na,nb);
        }
    }
    template<class G> void part2a(const G& g) {
        for (std::size_t i=0;i<g.nt;++i) cx(g,g.t(i).cx_edges);
    }
};
template<class G> inline u64 resolve_normal(const G& g,u32 u,u32 a) {
    const auto tv=g.utab[u];
    if (tv&COMPLEX) {
        const auto r=g.uf.canon_root(keyrec::scramble((u64(u)<<9)|a,g.ukey));
        if (r) return TAG_UF|*r;
    } else if (((tv&MASK)>>a)&1) return (u64(u)<<4)|15;
    return (u64(u)<<4)|a;
}
template<class G> inline u64 resolve_token(const G& g,u64 tok) {
    if (!(tok&RAW)) {
        if (auto r=g.uf.canon_root(tok)) return TAG_UF|*r;
        return TAG_ODDC|tok;
    }
    if (auto r=g.uf.raw_root(g,g.raw_bytes(tok))) return TAG_UF|*r;
    sys::fallback("raw user ID was not interned");
}
template<class G> inline void name_of(const G& g,u64 pkey,std::vector<u8>& out) {
    out.clear();
    if ((pkey>>62)==0) {
        const auto u=u32(pkey>>4),code=u32(pkey&15);
        const auto mask=code==15 ? u32(g.utab[u]&MASK) : u32{1}<<code;
        auto [v,len]=core::simple_name(g,u,mask);
        out.push_back('u');
        for (std::size_t i=0;i<len;++i) out.push_back(u8(v>>(56-i*8)));
    } else if ((pkey>>62)==1) {
        const auto n=g.uf.name(static_cast<u32>(pkey)); out.insert(out.end(),n.begin(),n.end());
    } else g.id_str(pkey&(RAW-1),'u',out);
}
}
