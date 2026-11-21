#include "../../cpp/pipeline/parse_packets.hpp"
#include <cassert>
#include <cstdio>
#include <random>

using namespace pipeline;
struct Arena {
    std::vector<void*> blocks;
    u8* alloc(std::size_t n) {
        auto* p=std::aligned_alloc(parse::CHUNK,n);if(!p) throw std::bad_alloc{};
        blocks.push_back(p);return static_cast<u8*>(p);
    }
    ~Arena(){for(auto* p:blocks)std::free(p);}
};
static bool same(Rec a,Rec b) {
    return a.w1==b.w1 && (a.w0==b.w0 || ((a.w0>>33)==INVALID_TS && (b.w0>>33)==INVALID_TS));
}
int main() {
    constexpr std::size_t nb=5;constexpr u64 uk=0xd795630ab,ek=0xa67340235;
    constexpr i64 base=1700000000000;
    std::vector<u8> products(PBOUND+65,8);products[4]=14;
    for(bool nt:{false,true}) {
        Arena arena;parse::NT=nt;parse::EvPart part(arena,nb);
        std::array<std::vector<Rec>,nb> packed,esc;
        std::mt19937_64 rng(58571);
        auto in=std::make_unique<EvBatch>();auto scalar=std::make_unique<parse::Out>();
        auto raw=std::make_unique<parse::Out>();auto vec=std::make_unique<parse::Out>();
        parse::EvP args{base,base,1209600000,uk,ek,parse::R12_USER_LIMIT,nb*BSIZE,products.data(),
                        part.lines_ptr(),nb};
        for(std::size_t t=0;t<900;++t) {
            const auto n=1+t%64;
            for(std::size_t i=0;i<n;++i) {
                auto w=t*64+i,b=(t%4==0?0:rng()%nb);
                auto slot=(i%9==0?BSIZE-1:rng()%BSIZE);
                const auto v=w%27;
                const u64 u=v==0?parse::R12_USER_LIMIT:(v==1?parse::R12_USER_LIMIT-1:rng()%1000);
                in->eid[i]=v==2?odd_id(i,6):keyrec::scramble(b*BSIZE+slot,ek);
                in->uid[i]=v==3?ID_EMPTY:v==4?odd_id(i,3):keyrec::scramble((u<<9)|(rng()%NALIAS),uk);
                in->ts[i]=v==5?TS_BAD:base+i64(rng()%args.wlen);
                in->ing[i]=base+i64((v==6?0:v==7?ING_MAX-1:rng()%(ING_MAX-1))*1000);
                in->pp[i]=ev_pp(v%5==1?KIND_PURCHASE:(v==8?KIND_OTHER:KIND_VIEW),v==9?PROD_EMPTY:4,
                                v%5==1?u32(1+rng()%10000000):0);
            }
            const auto a=parse::ev_scalar<true,true>(*in,*scalar,n,args);
            const auto v=parse::ev_vec<true,true>(*in,*vec,n,args);
            const auto w=parse::ev_scalar<true,false>(*in,*raw,n,args);
            assert(a.regular==v.regular && a.special==v.special && a.purchases==v.purchases &&
                   a.bad_ingest==v.bad_ingest && a.regular==w.regular && !a.bad_ingest);
            for(std::size_t i=0;i<n;++i) {
                assert(scalar->rec[i]==vec->rec[i] && scalar->ob[i]==vec->ob[i] && scalar->lp[i]==vec->lp[i]);
                if(!(a.regular>>i&1)) {assert(vec->ob[i]==nb);continue;}
                const auto b=vec->ob[i];const Rec expected{raw->rec[i][0],raw->rec[i][1]};
                if(a.purchases>>i&1) {
                    const auto [q,x]=vec->rec[i];const auto cat=products[std::min<std::size_t>(u32(in->pp[i]),PBOUND)];
                    Rec full{std::rotl(x,32)|(u64(cat)<<4),q&parse::R12_MASK};
                    assert(same(full,expected));part.defer(b,full.w0,full.w1);esc[b].push_back(expected);
                    vec->ob[i]=nb;vec->lp[i]=std::uintptr_t(args.lines)+nb*parse::R12_STAGE;
                } else packed[b].push_back(expected);
            }
            part.push_encoded(n,vec->ob.data(),vec->lp.data(),vec->rec.data());part.flush_escapes();
            if(t%5==0) {
                const auto b=t%nb;Rec full{rng(),(ODDU<<37)|u64(t%BSIZE)};
                part.push(b,full.w0,full.w1);esc[b].push_back(full);
            }
        }
        part.finish();
        for(std::size_t b=0;b<nb;++b) {
            std::size_t i=0,j=0,links=0;const i8* expect=nullptr;
            part.for_each_chunk(b,[&](const u8* p,std::size_t n,const i8* next){
                if(expect) assert(reinterpret_cast<const i8*>(p)==expect);
                assert(n<=parse::LINK_OFF/12 && n);
                if(next) {++links;assert(n==parse::LINK_OFF/12);}
                for(std::size_t k=0;k<n;++k) assert(same(parse::unpack_event12(p+12*k),packed[b].at(i++)));
                expect=next;
            });
            part.for_each_esc(b,[&](Rec r){assert(same(r,esc[b].at(j++)));});
            assert(i==packed[b].size() && j==esc[b].size() && part.count(b)==i && !expect && links>0);
        }
    }
    std::puts("12-byte packets, full escape records, page chains, sink buckets and both store modes passed");
}
