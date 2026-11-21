#include "parse_packets.hpp"
#include <cassert>
#include <cstdio>
#include <thread>
#include <vector>
using namespace pipeline;
constexpr u64 uk=0x5594e98f4,ek=0x374aae9d22,ck=0xff0962efd,base=1720800000000ULL;
int main() {
    std::vector<u8> cats(PBOUND+65,13),camp(CBOUND+64,255);
    cats[0]=9; cats[3]=7;
    for (std::size_t i=3;i<=9;++i) camp[i]=i%3;
    auto a=std::make_unique<parse::Out>(),b=std::make_unique<parse::Out>();
    for (unsigned seed=0;seed<320;++seed) {
        EvBatch ev; CkBatch cl;
        for (std::size_t i=0;i<NB;++i) {
            const auto v=(seed*29+i*7u)%109;
            ev.uid[i]=v%21==0 ? ID_EMPTY : (v%17==0 ? odd_id(i,3) : keyrec::scramble((u64(v%19)<<9)|(v%18),uk));
            ev.eid[i]=v%13==0 ? odd_id(i,2) : keyrec::scramble((i*16384ull+v)%900000,ek);
            ev.ts[i]=base+(v%9==0 ? 99999 : v*127);
            ev.ing[i]=v%23==0 ? TS_BAD : i64(ev.ts[i]+(v%4==0 ? DAY_MS : (v%4==1 ? DAY_MS+1 : 12)));
            ev.pp[i]=ev_pp(v%4,v%29==0 ? PROD_EMPTY : (v%31==0 ? PBOUND+7 : v%16),v%13 ? v%513 : 0);
            cl.ts[i]=v%31==0 ? TS_BAD : ev.ts[i]; cl.uid[i]=ev.uid[i];
            cl.cid[i]=v%13==0 ? odd_id(i,2) : keyrec::scramble((i*7+v)%512,ck);
            cl.camp[i]=v%17==0 ? CBOUND : v%12;
        }
        const std::size_t n=seed%(NB+1);
        parse::EvP p{i64(base),i64(base),99999,uk,ek,17,820000,cats.data(),cats.data()};
        const auto x=parse::ev_vec<true>(ev,*a,n,p); const auto y=parse::ev_scalar<true>(ev,*b,n,p);
        assert(x.regular==y.regular && x.special==y.special && x.bad_ingest==y.bad_ingest);
        for(std::size_t i=0;i<n;++i) {
            const u64 user=keyrec::unscramble(ev.uid[i],uk),id=keyrec::unscramble(ev.eid[i],ek);
            const bool valid=ev.uid[i]!=ID_EMPTY && u64(ev.ts[i])-base<p.wlen && !(ev.pp[i]&PP_INVALID) && std::bit_cast<i64>(u64(ev.ing[i])-u64(ev.ts[i]))<=DAY_MS;
            const bool regular=ev.eid[i]<=keyrec::MASK && id<p.ecap && (!valid || (ev.uid[i]<=keyrec::MASK && (user>>9)<p.ucap && (user&511)<NALIAS));
            assert(bool((x.regular>>i)&1)==regular && a->rec[i]==b->rec[i] && a->ob[i]==id/BSIZE && a->lp[i]==b->lp[i]);
            if(regular && !valid) assert(a->rec[i][0]==UINT64_MAX);
        }
        for(bool range:{false,true}) {
            auto span=range ? std::optional(std::pair<u64,u64>{3,6}) : std::nullopt;
            parse::CkP q{i64(base),99999,uk,17,ck,413,camp.data(),span,camp.data()};
            auto run=[&]<bool R,bool A> {
                const auto x=parse::ck_vec<R,A>(cl,*a,n,q), y=parse::ck_scalar<R,A>(cl,*b,n,q);
                assert(x.regular==y.regular && x.special==y.special);
                for(std::size_t i=0;i<n;++i) assert(a->rec[i]==b->rec[i] && a->ob[i]==b->ob[i] && a->lp[i]==b->lp[i]);
                return x;
            };
            if(range) run.template operator()<true,false>(); else run.template operator()<false,false>();
            const auto m=range ? run.template operator()<true,true>() : run.template operator()<false,true>();
            std::vector<std::array<u64,2>> indexed(413), expected(413);
            ckix::CkxP qp{i64(base),99999,uk,17,ck,413,camp.data(),span,indexed.data()};
            ckix::Pend pd;
            assert(ckix::ck_vec_ix(cl,pd,n,qp)==m.special);
            ckix::flush(pd,qp.csr);
            u64 end=0;
            for(std::size_t i=0;i<n;++i) if ((m.regular>>i)&1) {
                auto id=a->rec[i][0]&keyrec::MASK;
                const auto user=keyrec::unscramble(cl.uid[i],uk)>>9;
                expected[id]={a->rec[i][1],user|ckix::VALID};
                end=std::max(end,id+1);
            }
            assert(indexed==expected && pd.end==end);
        }
    }
    std::array<u64,2> pair{};
    auto write=[&]{for(int i=0;i<2000;++i) ckix::store_pair(pair,7,8);};
    std::thread t(write); write(); t.join(); assert(pair[0]==7 && pair[1]==8);
    std::puts("routing packet masks, packed words, delayed click index and duplicate writers passed");
}
