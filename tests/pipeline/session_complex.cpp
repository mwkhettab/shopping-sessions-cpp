#include "../../cpp/pipeline/model.hpp"
#include "../../cpp/pipeline/complex.hpp"
#include <cassert>
#include <iostream>
#include <map>
#include <tuple>
using namespace pipeline;
using K=std::array<u64,2>;
using Row=std::tuple<std::string,i32,i32,i32,i32,u8>;
struct Person {std::vector<Rec> events;std::vector<K> clicks;};
u64 w(u32 t,u32 a,u32 price,u32 cat){return (u64(t)<<33)|(u64(price)<<9)|(cat<<4)|a;}
u64 click(u32 t,u32 a,u32 c){return (u64(t)<<32)|(c<<4)|a;}
void collect(const core::Core& c,std::vector<Row>& out) {
    const auto* off=c.names_off.as_ptr();const auto* names=c.names.as_ptr();
    for(std::size_t i=0;i<c.day.len();++i) {
        const auto p=c.keys.as_ptr()[i];
        out.emplace_back(std::string(names+off[p],names+off[p+1]),c.day.as_ptr()[i],c.ses.as_ptr()[i],
                         c.evs.as_ptr()[i],c.rev.as_ptr()[i],c.cat.as_ptr()[i]);
    }
}
int main() {
    G g;g.nt=2;g.nd=13;g.nc=5;g.day0=4;g.ukey=771;g.row_cap=5000;g.ucap=64;
    g.workers.emplace_back(new TState);g.workers.emplace_back(new TState);
    for(auto& worker:g.workers) worker->core=core::Core(g.nc,g.nd,g.row_cap,0,g.day0);
    std::array<u16,65> utab{};std::array<std::atomic<u64>,1> claim{};
    std::array<u32,66> off{};std::vector<K> csr;
    g.utab=utab.data();g.claim=claim.data();g.csr_off=off.data();
    utab[3]=COMPLEX|EXTRA;utab[4]=3;utab[10]=EXTRA|1;
    auto token=[&](u32 u,u32 a){return keyrec::scramble((u64(u)<<9)|a,g.ukey);};
    auto strid=[&](u64 id,char prefix){std::vector<u8> b;g.id_str(id,prefix,b);return std::string(b.begin(),b.end());};
    auto simple=[&](u32 u,u32 mask){
        std::string best;
        for(u32 i=0;i<14;++i) if (mask&(1u<<i)) {
            auto str=strid(token(u,i),'u');if(best.empty() || str<best) best=str;
        }
        return best;
    };
    auto raw=[&](std::size_t tid,std::string str){
        auto& t=g.t(tid);const auto i=t.raws.size();
        t.raws.push_back({t.rawbuf.size(),u32(str.size())});
        t.rawbuf.insert(t.rawbuf.end(),str.begin(),str.end());
        return RAW|(tid<<34)|i;
    };
    const auto a=raw(0,"A-person"),b=raw(1,"A-person"),k1=raw(1,"k0a"),k2=raw(0,"k_!");
    const auto k3=raw(0,"0"),odd=keyrec::scramble((g.ucap+100)<<9,g.ukey);
    const u32 pa=g.uf.node(g,a),pb=g.uf.node(g,b),pn=g.uf.node(g,token(3,0));
    assert(pa==pb);
    g.uf.join(pa,pn);g.uf.finish(g);
    std::map<std::string,Person> persons;
    const auto name3=std::string("A-person"),n31=simple(3,2),n4=simple(4,3),n44=simple(4,16),
        n10=simple(10,1),nodd=strid(odd,'u');
    auto reg=[&](u32 u,u32 a)->const std::string&{
        if(u==3) return a?n31:name3;
        if(u==4) return a==4?n44:n4;
        return n10;
    };
    for(u32 i=0;i<200;++i) {
        const auto u=i%2?3u:4u;
        const u32 al=u==3?((i>>1)&1):i%6==0?4:((i>>1)&1);
        const auto word=w(((i*137)%71)*2400000,al,i%7?0:400+i,i%9);
        g.t(i%2).cx_raw.push_back({word,u});persons[reg(u,al)].events.push_back({word,0});
    }
    g.t(0).cx_users.push_back(3);g.t(1).mixed_users.push_back(4);
    g.t(0).odd_users={a,odd};g.t(1).odd_users={b,odd};
    for(unsigned i=0;i<5;++i) {
        const u32 tid=i&1,ix=(i>>1)&1;
        const auto word=w(145000000+i*127,0,i*2,5);
        const auto ref=ODDU|(u64(tid)<<22)|ix;
        (i<3 ? g.t(1-tid).cx_raw : g.t(0).cx_raw).push_back({word,ref});
        persons[ix?nodd:name3].events.push_back({word,0});
    }
    const auto ow=w(112000000,0,120,7);
    g.odd_kept.push_back({0,0,ow,ODDU|(1u<<22)});persons[name3].events.push_back({ow,0});
    for(u32 u:{3,10}) {
        const auto word=w(52000000,0,199,9);g.extra[u].push_back(word);persons[reg(u,0)].events.push_back({word,0});
    }
    for(u32 u=0;u<65;++u) {
        off[u]=csr.size();
        if(u!=3 && u!=4 && u!=10) continue;
        const u32 nk=u==4?461:17;
        std::vector<K> cs;
        for(u32 i=0;i<nk;++i) {
            const auto al=u==4?(i%3?0u:4u):u==10?0u:i%2;
            const u64 cid=i%13==0?k1:i%13==1?k2:i%13==2?k3:u64((i*31+u)%379+1);
            auto row=K{click((i/4)*1300000+600000000,al,(i/2)%5),cid};
            cs.push_back(row);persons[reg(u,al)].clicks.push_back(row);
        }
        std::sort(cs.begin(),cs.end(),[&](const auto& x,const auto& y){
            return (x[0]>>32)!=(y[0]>>32)?(x[0]>>32)<(y[0]>>32):strid(x[1],'k')>strid(y[1],'k');
        });csr.insert(csr.end(),cs.begin(),cs.end());
    }
    off.back()=csr.size();g.csr=csr.data();g.csr_cap=csr.size();
    for(u32 i=0;i<30;++i) {
        auto id=i%3?odd:b,cid=i%2?k1:u64(i+1);
        auto row=K{click(605000000+(i/3)*10000,0,(i+1)%5),cid};
        g.t(i&1).odd_clicks.push_back({id,row[0],row[1]});
        persons[id==odd?nodd:name3].clicks.push_back(row);
    }
    complex::stage4(g);
    core::Core oracle(g.nc,g.nd,g.row_cap,0,g.day0);
    for(auto& [name,p]:persons) {
        std::sort(p.events.begin(),p.events.end(),[](auto x,auto y){return x.w0<y.w0;});
        std::sort(p.clicks.begin(),p.clicks.end(),[&](const auto& a,const auto& b){
            return (a[0]>>32)!=(b[0]>>32)?(a[0]>>32)<(b[0]>>32):strid(a[1],'k')>strid(b[1],'k');
        });
        oracle.person(g,p.events,p.clicks,core::Name::Bytes({reinterpret_cast<const u8*>(name.data()),name.size()}));
    }
    std::vector<Row> got,want;collect(g.t(0).core,got);collect(g.t(1).core,got);collect(oracle,want);
    std::sort(got.begin(),got.end());std::sort(want.begin(),want.end());assert(got==want);
    for(std::size_t i=0;i<oracle.agg.len();++i) for(unsigned j=0;j<2;++j) {
        assert(oracle.agg[i][j]==g.t(0).core.agg[i][j]+g.t(1).core.agg[i][j]);
        assert(oracle.aggp[i][j]==g.t(0).core.aggp[i][j]+g.t(1).core.aggp[i][j]);
    }
    std::cout<<persons.size()<<" complex/mixed/extra people, "<<got.size()<<" daily rows, 525 click rows matched\n";
}
