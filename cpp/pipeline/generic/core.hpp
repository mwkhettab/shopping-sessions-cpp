#pragma once
#include "parse.hpp"
#include <numeric>

namespace pipeline::generic {
struct LinkMap {
    Tab tab;
    std::vector<B> ids;
    std::vector<u64> hashes;
    std::vector<u32> name_of;
    std::pair<B,u64> name(B user,u64 h) const {
        auto hit=tab.find(h,[&](u32 i){return ids[i]==user;});
        if(!hit.index) return {user,h};
        auto i=name_of[*hit.index];
        return {ids[i],hashes[i]};
    }
};
struct Link { B a;u64 ha;B b;u64 hb; };
using LinkPart=std::vector<Link>;
inline LinkPart parse_links(B bytes) {
    LinkPart out;
    out.reserve(count_lines(bytes)+1);
    for_lines(bytes,[&](B line){
        auto f=split<2>(line);
        if(f && !(*f)[0].empty() && !(*f)[1].empty())
            out.push_back({(*f)[0],hash((*f)[0]),(*f)[1],hash((*f)[1])});
    });
    return out;
}
inline u32 root(std::vector<u32> &p,u32 x) {
    auto r=x;
    while(p[r]!=r) r=p[r];
    while(p[x]!=r) {auto n=p[x];p[x]=r;x=n;}
    return r;
}
inline LinkMap build_links(std::span<const LinkPart> parts) {
    std::size_t n=0;
    for(const auto &p:parts) n+=p.size();
    LinkMap map{Tab(2*n+16),{},{},{}};
    std::vector<u32> parent;
    parent.reserve(n);map.ids.reserve(n);map.hashes.reserve(n);
    auto intern=[&](B s,u64 h) {
        auto hit=map.tab.find(h,[&](auto i){return map.ids[i]==s;});
        if(hit.index) return *hit.index;
        auto i=u32(map.ids.size());
        map.ids.push_back(s);map.hashes.push_back(h);parent.push_back(i);
        map.tab.set(hit.slot,h,i);
        return i;
    };
    for(const auto &p:parts) for(auto v:p) {
        auto x=intern(v.a,v.ha),y=intern(v.b,v.hb);
        auto rx=root(parent,x),ry=root(parent,y);
        if(rx!=ry) parent[std::max(rx,ry)]=std::min(rx,ry);
    }
    const auto m=map.ids.size();
    std::vector<u32> min_of(m);
    std::iota(min_of.begin(),min_of.end(),0);
    map.name_of.resize(m);
    for(u32 i=0;i<m;++i) {
        u32 r=root(parent,i);
        map.name_of[i]=r;
        if(map.ids[i]<map.ids[min_of[r]]) min_of[r]=i;
    }
    for(auto &r:map.name_of) r=min_of[r];
    return map;
}

inline constexpr u32 T_VIEW=0,T_CART=1,T_PURCHASE=2,T_INVALID=255;
struct Ev {
    const char *line;
    i64 ingest,ts;
    u32 pt,cat;
    B eid() const {return first(line);}
    B user() const {return first_two(line).second;}
};
struct Rec {B name;u64 nh;i64 ts;u32 pt,cat;};
template<class T> struct Parts {
    std::vector<T> items;
    std::vector<u32> off;
    static Parts group(std::span<const std::pair<T,u32>> rows,std::size_t n) {
        Parts p;
        p.off.assign(n+1,0);
        for(const auto &r:rows) ++p.off[r.second+1];
        std::partial_sum(p.off.begin(),p.off.end(),p.off.begin());
        std::vector<u32> pos(p.off.begin(),p.off.end()-1);
        p.items.resize(rows.size());
        for(const auto &r:rows) p.items[pos[r.second]++]=r.first;
        return p;
    }
    std::span<const T> shard(std::size_t n) const {
        if(off.empty()) return {};
        return std::span<const T>(items).subspan(off[n],off[n+1]-off[n]);
    }
};
struct EvCtx {i64 ws,we;const Map<B,u32> &products;u32 unknown_cat;};
inline u32 shard_of(u64 h,std::size_t n) {return u32((h>>32)&(n-1));}
inline Parts<Ev> parse_events(B bytes,const EvCtx &ctx,std::size_t n,std::vector<std::pair<Ev,u32>> &tmp) {
    tmp.clear();
    for_lines(bytes,[&](B line) {
        auto f=split<7>(line);
        if(!f) return;
        auto [eid,user,ts,ingest,typ,product,price]=*f;
        if(eid.empty()) return;
        auto it=parse_ts(ingest);
        if(!it) return;
        Ev e{line.data(),*it,0,T_INVALID<<24,0};
        auto validate=[&]{
            if(user.empty() || product.empty()) return;
            auto et=parse_ts(ts);
            if(!et || *et<ctx.ws || *et>=ctx.we) return;
            u32 ty=typ=="view"?T_VIEW:typ=="cart"?T_CART:typ=="purchase"?T_PURCHASE:T_INVALID;
            if(ty==T_INVALID) return;
            u32 pr=0;
            if(ty==T_PURCHASE) {
                auto v=parse_price(price);
                if(!v) return;
                pr=*v;
                auto c=ctx.products.find(product);
                e.cat=c==ctx.products.end()?ctx.unknown_cat:c->second;
            }
            if(*it-*et>DAY_MS) return;
            e.ts=*et;e.pt=pr|(ty<<24);
        };
        validate();
        tmp.emplace_back(e,shard_of(hash(eid),n));
    });
    return Parts<Ev>::group(tmp,n);
}
struct DedupBufs {
    Tab tab;
    std::vector<u32> base;
    std::vector<bool> win;
    std::vector<std::pair<Rec,u32>> tmp;
};
inline Parts<Rec> dedup_route(std::span<const Parts<Ev>> parts,std::size_t s,const LinkMap &links,
                              std::size_t n,DedupBufs &b) {
    b.base.clear();
    std::size_t total=0;
    for(const auto &p:parts) {b.base.push_back(u32(total));total+=p.shard(s).size();}
    b.base.push_back(u32(total));
    auto locate=[&](u32 index)->const Ev& {
        auto ci=std::upper_bound(b.base.begin(),b.base.end(),index)-b.base.begin()-1;
        return parts[ci].shard(s)[index-b.base[ci]];
    };
    b.tab.reset(total);
    b.win.assign(total,true);
    u32 flat=0;
    for(const auto &p:parts) for(const auto &e:p.shard(s)) {
        auto eid=e.eid();auto h=hash(eid);
        auto match=b.tab.find(h,[&](u32 i){return locate(i).eid()==eid;});
        if(!match.index) b.tab.set(match.slot,h,flat);
        else if(e.ingest<locate(*match.index).ingest) {
            b.win[*match.index]=false;
            b.tab.set(match.slot,h,flat);
        } else b.win[flat]=false;
        ++flat;
    }
    b.tmp.clear();
    std::size_t k=0;
    for(const auto &p:parts) for(const auto &e:p.shard(s)) {
        if(!b.win[k++] || (e.pt>>24)>T_PURCHASE) continue;
        auto user=e.user();
        auto [name,nh]=links.name(user,hash(user));
        b.tmp.emplace_back(Rec{name,nh,e.ts,e.pt,e.cat},shard_of(nh,n));
    }
    return Parts<Rec>::group(b.tmp,n);
}
struct Ck0 {const char *line;i64 ts;u32 camp;};
struct Ck {B name;u64 nh;const char *line;i64 ts;u32 camp;};
inline std::vector<Ck0> parse_clicks(B bytes,i64 ws,i64 we,const Map<B,u32> &campaigns) {
    auto lo=ws-7*DAY_MS;
    std::vector<Ck0> out;
    out.reserve(count_lines(bytes)+1);
    for_lines(bytes,[&](B line){
        auto f=split<4>(line);
        if(!f) return;
        const auto &[cid,user,camp,ts]=*f;
        if(cid.empty() || user.empty()) return;
        auto t=parse_ts(ts);
        if(!t || *t<lo || *t>=we) return;
        auto found=campaigns.find(camp);
        if(found!=campaigns.end()) out.push_back({line.data(),*t,found->second});
    });
    return out;
}
inline Parts<Ck> route_clicks(std::span<const Ck0> clicks,const LinkMap &links,std::size_t n,
                              std::vector<std::pair<Ck,u32>> &tmp) {
    tmp.clear();
    for(const auto &c:clicks) {
        auto user=first_two(c.line).second;
        auto [name,nh]=links.name(user,hash(user));
        tmp.emplace_back(Ck{name,nh,c.line,c.ts,c.camp},shard_of(nh,n));
    }
    return Parts<Ck>::group(tmp,n);
}

inline constexpr i64 GAP_MS=1800000,WEEK_MS=7*DAY_MS;
using Acc=Map<u64,std::array<u64,4>>;
inline u64 key(u32 camp,i32 day) {return (u64(camp)<<32)|u32(day);}
inline std::pair<u32,i32> unkey(u64 k) {return {u32(k>>32),i32(u32(k))};}
struct PE {i64 ts;u32 pt,cat;};
struct PC {i64 ts;B cid;u32 camp;};
struct Sess {i64 start;i32 day;u64 rev;bool purch;};
struct UserRow {B name;i32 day;u64 sessions,events,revenue;std::optional<u32> top;};
struct Scratch {
    std::vector<Sess> sess;
    std::vector<i64> starts;
    std::vector<i32> days;
    std::vector<u32> day_end;
    std::vector<u64> catsum;
    std::vector<u32> touched;
    std::vector<std::pair<u32,i64>> ct;
    std::vector<u32> rs;
    explicit Scratch(std::size_t ncat):catsum(ncat){}
};
struct DayAcc {i32 day;u64 sessions,events,revenue;};
inline void flush_row(B name,const DayAcc &d,Scratch &sc,std::vector<UserRow> &rows) {
    std::optional<u32> top;
    u64 best=0;
    for(auto c:sc.touched) {
        auto v=sc.catsum[c];
        if(!top || v>best || (v==best && c<*top)) {best=v;top=c;}
        sc.catsum[c]=0;
    }
    sc.touched.clear();
    rows.push_back({name,d.day,d.sessions,d.events,d.revenue,top});
}
inline void credit(std::span<const Sess> sessions,std::span<const PC> clicks,std::vector<u32> &rs,u32 organic,Acc &acc) {
    rs.clear();
    for(std::size_t i=0;i<clicks.size();++i)
        rs.push_back(i && clicks[i].ts==clicks[i-1].ts?rs[i-1]:u32(i));
    std::size_t lo=0,hi=0;
    for(const auto &s:sessions) {
        while(hi<clicks.size() && clicks[hi].ts<=s.start) ++hi;
        while(lo<hi && clicks[lo].ts<s.start-WEEK_MS) ++lo;
        auto camp=hi>lo?clicks[rs[hi-1]].camp:organic;
        auto &v=acc[key(camp,s.day)];
        ++v[0];v[1]+=s.purch;v[2]+=s.rev;
    }
}
inline void assists(Scratch &sc,std::span<const PC> clicks,Acc &acc) {
    auto &ct=sc.ct;
    ct.clear();
    for(const auto &c:clicks) ct.emplace_back(c.camp,c.ts);
    std::sort(ct.begin(),ct.end());
    for(std::size_t i=0;i<ct.size();) {
        auto [camp,start]=ct[i++];
        auto end=start+WEEK_MS;
        while(i<ct.size() && ct[i].first==camp && ct[i].second<=end) {
            end=std::max(end,ct[i].second+WEEK_MS);
            ++i;
        }
        auto lo=std::size_t(std::lower_bound(sc.starts.begin(),sc.starts.end(),start)-sc.starts.begin());
        auto hi=std::size_t(std::upper_bound(sc.starts.begin(),sc.starts.end(),end)-sc.starts.begin());
        for(auto k=lo;k<hi;) {
            auto next=std::min<std::size_t>(sc.day_end[k],hi);
            acc[key(camp,sc.days[k])][3]+=next-k;
            k=next;
        }
    }
}
inline void person(B name,std::span<PE> evs,std::span<PC> cks,Scratch &sc,std::vector<UserRow> &rows,
                   u32 organic,Acc &acc) {
    if(evs.empty()) return;
    std::sort(evs.begin(),evs.end(),[](auto &a,auto &b){return a.ts<b.ts;});
    sc.sess.clear();
    auto prev=evs.front().ts;
    DayAcc d{day_of(prev),0,0,0};
    for(const auto &e:evs) {
        if(sc.sess.empty() || e.ts-prev>GAP_MS) {
            auto day=day_of(e.ts);
            if(day!=d.day) {flush_row(name,d,sc,rows);d={day,0,0,0};}
            ++d.sessions;
            sc.sess.push_back({e.ts,day,0,false});
        }
        prev=e.ts;
        ++d.events;
        if((e.pt>>24)==T_PURCHASE) {
            auto price=e.pt&0x00ffffff;
            sc.sess.back().rev+=price;
            sc.sess.back().purch=true;
            d.revenue+=price;
            if(!sc.catsum[e.cat]) sc.touched.push_back(e.cat);
            sc.catsum[e.cat]+=price;
        }
    }
    flush_row(name,d,sc,rows);
    std::sort(cks.begin(),cks.end(),[](const auto &a,const auto &b){
        return a.ts!=b.ts?a.ts<b.ts:a.cid<b.cid;
    });
    credit(sc.sess,cks,sc.rs,organic,acc);
    if(!cks.empty()) {
        sc.starts.clear();sc.days.clear();
        for(auto s:sc.sess) {sc.starts.push_back(s.start);sc.days.push_back(s.day);}
        sc.day_end.resize(sc.sess.size());
        u32 end=u32(sc.sess.size());
        for(std::size_t i=sc.sess.size();i--;) {
            if(i+1<sc.sess.size() && sc.days[i+1]!=sc.days[i]) end=u32(i+1);
            sc.day_end[i]=end;
        }
        assists(sc,cks,acc);
    }
}
struct ShardBufs {
    Tab tab;
    std::vector<B> names;
    std::vector<u32> pid_ev,off,pos;
    std::vector<PE> pe;
    std::vector<u32> pid_ck,coff;
    std::vector<PC> pc;
};
inline std::vector<UserRow> run_shard(std::span<const Parts<Rec>> recs,std::span<const Parts<Ck>> cks,
                                      std::size_t p,u32 organic,Scratch &sc,ShardBufs &bf,Acc &acc) {
    std::size_t n_ev=0;
    for(const auto &r:recs) n_ev+=r.shard(p).size();
    bf.tab.reset(n_ev);bf.names.clear();bf.pid_ev.clear();
    for(const auto &r:recs) for(const auto &e:r.shard(p)) {
        auto hit=bf.tab.find(e.nh,[&](auto i){return bf.names[i]==e.name;});
        auto pid=hit.index.value_or(u32(bf.names.size()));
        if(!hit.index) {bf.names.push_back(e.name);bf.tab.set(hit.slot,e.nh,pid);}
        bf.pid_ev.push_back(pid);
    }
    auto np=bf.names.size();
    bf.off.assign(np+1,0);
    for(auto i:bf.pid_ev) ++bf.off[i+1];
    std::partial_sum(bf.off.begin(),bf.off.end(),bf.off.begin());
    bf.pos.assign(bf.off.begin(),bf.off.end()-1);
    bf.pe.resize(n_ev);
    std::size_t k=0;
    for(const auto &r:recs) for(const auto &e:r.shard(p)) {
        auto pid=bf.pid_ev[k++];
        bf.pe[bf.pos[pid]++]={e.ts,e.pt,e.cat};
    }
    bf.pid_ck.clear();bf.coff.assign(np+1,0);
    for(const auto &c:cks) for(const auto &x:c.shard(p)) {
        auto hit=bf.tab.find(x.nh,[&](auto i){return bf.names[i]==x.name;});
        if(hit.index) ++bf.coff[*hit.index+1];
        bf.pid_ck.push_back(hit.index.value_or(UINT32_MAX));
    }
    std::partial_sum(bf.coff.begin(),bf.coff.end(),bf.coff.begin());
    bf.pos.assign(bf.coff.begin(),bf.coff.end()-1);
    bf.pc.resize(bf.coff[np]);
    k=0;
    for(const auto &c:cks) for(const auto &x:c.shard(p)) {
        auto pid=bf.pid_ck[k++];
        if(pid!=UINT32_MAX) bf.pc[bf.pos[pid]++]={x.ts,first(x.line),x.camp};
    }
    std::vector<UserRow> rows;
    rows.reserve(np*2);
    for(std::size_t i=0;i<np;++i) {
        auto es=std::span<PE>(bf.pe).subspan(bf.off[i],bf.off[i+1]-bf.off[i]);
        auto cs=std::span<PC>(bf.pc).subspan(bf.coff[i],bf.coff[i+1]-bf.coff[i]);
        person(bf.names[i],es,cs,sc,rows,organic,acc);
    }
    return rows;
}
}
