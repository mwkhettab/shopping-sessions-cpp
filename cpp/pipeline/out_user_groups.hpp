#pragma once
#include "pqw.hpp"
#include <filesystem>
#include <optional>

namespace pipeline::out {
// A group has 4,096 rows unless ending the piece would leave fewer than 2,049.
inline constexpr std::size_t USER_ROWS=4096,USER_LIMIT=USER_ROWS+USER_ROWS/2;
using UserWords=std::array<i32,USER_LIMIT>;
inline constexpr auto USER_PERM=[] {
    std::array<std::array<u8,8>,256> table{};
    for(unsigned mask=0;mask<256;++mask) {
        unsigned j=0;
        for(unsigned k=0;k<8;++k) if(mask&(1u<<k)) table[mask][j++]=u8(k);
        for(unsigned k=8;k--; ) if(!(mask&(1u<<k))) table[mask][j++]=u8(k);
    }
    return table;
}();
struct UserGroup {
    std::array<u32,USER_LIMIT> keys;
    UserWords day,sessions,events,revenue;
    std::array<u8,USER_LIMIT> category;
    std::size_t head{},rows{},defined{};
    u32 kmin{},kmax{};
    std::array<i32,3> low{},high{};

    template<class V> void finish_split(const V& v,std::size_t a,std::size_t j,std::size_t end) {
        for(;j<rows;++j) {
            const auto i=a+j;const auto k=v.keys[i];
            const std::array<i32,3> values{v.day[i],v.ses[i],v.evs[i]};
            const auto rev=v.rev[i];const auto cat=v.cat[i];
            const auto at=!rev && cat==UINT8_MAX?head++:--end;
            keys[at]=k;day[at]=values[0];sessions[at]=values[1];events[at]=values[2];
            revenue[at]=rev;category[at]=cat;
            kmin=std::min(kmin,k);kmax=std::max(kmax,k);defined+=cat!=UINT8_MAX;
            for(unsigned c=0;c<3;++c) {low[c]=std::min(low[c],values[c]);high[c]=std::max(high[c],values[c]);}
        }
    }
#if defined(__x86_64__) && (defined(__clang__) || defined(__GNUC__))
    template<class V> [[gnu::target("avx2,popcnt")]] void split_wide(const V& v,std::size_t a) {
        using Vec=__m256i;
        const Vec one=_mm256_set1_epi32(-1),zero=_mm256_setzero_si256();
        Vec kminv=one,kmaxv=zero,mins[3],maxs[3];
        for(unsigned j=0;j<3;++j){mins[j]=_mm256_set1_epi32(INT32_MAX);maxs[j]=_mm256_set1_epi32(INT32_MIN);}
        std::size_t i=0,end=rows;
        const i32* input[]={v.day+a,v.ses+a,v.evs+a};
        i32* output[]={day.data(),sessions.data(),events.data()};
        for(;rows-i>=16;i+=8) {
            Vec values[3];
            const auto k=_mm256_loadu_si256(reinterpret_cast<const Vec*>(v.keys+a+i));
            const auto r=_mm256_loadu_si256(reinterpret_cast<const Vec*>(v.rev+a+i));
            const auto c=_mm_loadl_epi64(reinterpret_cast<const __m128i*>(v.cat+a+i));
            const unsigned cn=_mm_movemask_epi8(_mm_cmpeq_epi8(c,_mm_set1_epi8(-1)))&255u;
            const unsigned rz=_mm256_movemask_ps(_mm256_castsi256_ps(_mm256_cmpeq_epi32(r,zero)));
            const unsigned mask=rz&cn;
            const auto perm=_mm_loadl_epi64(reinterpret_cast<const __m128i*>(USER_PERM[mask].data()));
            const auto ix=_mm256_cvtepu8_epi32(perm);
            kminv=_mm256_min_epu32(kminv,k);kmaxv=_mm256_max_epu32(kmaxv,k);
            for(unsigned j=0;j<3;++j) {
                values[j]=_mm256_loadu_si256(reinterpret_cast<const Vec*>(input[j]+i));
                mins[j]=_mm256_min_epi32(mins[j],values[j]);maxs[j]=_mm256_max_epi32(maxs[j],values[j]);
            }
            const auto pos=end-8;
            const auto key_order=_mm256_permutevar8x32_epi32(k,ix);
            _mm256_storeu_si256(reinterpret_cast<Vec*>(keys.data()+head),key_order);
            _mm256_storeu_si256(reinterpret_cast<Vec*>(keys.data()+pos),key_order);
            for(unsigned j=0;j<3;++j) {
                const auto order=_mm256_permutevar8x32_epi32(values[j],ix);
                _mm256_storeu_si256(reinterpret_cast<Vec*>(output[j]+head),order);
                _mm256_storeu_si256(reinterpret_cast<Vec*>(output[j]+pos),order);
            }
            _mm256_storeu_si256(reinterpret_cast<Vec*>(revenue.data()+pos),_mm256_permutevar8x32_epi32(r,ix));
            _mm_storel_epi64(reinterpret_cast<__m128i*>(category.data()+pos),_mm_shuffle_epi8(c,perm));
            const auto z=std::popcount(mask);head+=z;end-=8-z;defined+=8-std::popcount(cn);
        }
        std::array<u32,8> ktmp;
        _mm256_storeu_si256(reinterpret_cast<Vec*>(ktmp.data()),kminv);kmin=*std::min_element(ktmp.begin(),ktmp.end());
        _mm256_storeu_si256(reinterpret_cast<Vec*>(ktmp.data()),kmaxv);kmax=*std::max_element(ktmp.begin(),ktmp.end());
        std::array<i32,8> tmp;
        for(unsigned j=0;j<3;++j) {
            _mm256_storeu_si256(reinterpret_cast<Vec*>(tmp.data()),mins[j]);low[j]=*std::min_element(tmp.begin(),tmp.end());
            _mm256_storeu_si256(reinterpret_cast<Vec*>(tmp.data()),maxs[j]);high[j]=*std::max_element(tmp.begin(),tmp.end());
        }
        finish_split(v,a,i,end);
    }
#endif
    template<class V> void split(const V& v,std::size_t a,std::size_t n) {
        rows=n;head=0;defined=0;kmin=UINT32_MAX;kmax=0;
        low.fill(INT32_MAX);high.fill(INT32_MIN);
#if defined(__x86_64__) && (defined(__clang__) || defined(__GNUC__)) && !defined(SHOP_SPARSE_FORCE_SCALAR)
        if(n>=16 && __builtin_cpu_supports("avx2") && __builtin_cpu_supports("popcnt")) {
            split_wide(v,a);
            return;
        }
#endif
        finish_split(v,a,0,n);
    }
};

inline pqw::Meta range_chunk(pqw::Out& o,std::span<const i32> values,i32 lo,i32 hi,bool automatic) {
    using namespace pqw;
    const auto n=values.size();const auto start=o.pos();
    Meta m{1,false,0,start,0,n};Plan p;
    const u64 range=u64(i64(hi)-i64(lo));
    if(range<(1U<<20)) {
        const auto card=std::size_t(range)+1;const auto w=bits_for(range);
        if(!automatic || (card*4+(n+7)/8*w)*10<=n*32) {
            p.form=Plan::Dense;p.min=lo;p.card=card;p.w=w;
        }
    }
    if(p.form==Plan::Dense && p.card==1) {
        page(o,2,4,1,0);o.put_le(lo);m.dict=true;m.dict_off=start;m.data_off=o.pos();
        page(o,0,varlen(2*n)+2,n,8);o.put_le(u8{1});o.put_varint(2*n);o.put_le(u8{0});
    } else emit_ints(o,values,p,m);
    m.size=o.pos()-start;return m;
}

inline std::size_t encode_key_runs(std::span<u8> bytes,std::span<const u32> keys,u32 base,u32 width) {
    const auto value_bytes=(width+7)/8;
    std::size_t out=0;
    for(std::size_t i=0;i<keys.size();) {
        std::size_t end=i+1;
        while(end<keys.size() && end-i<63 && keys[end]==keys[i]) ++end;
        bytes[out++]=u8(2*(end-i));
        const u32 value=keys[i]-base;
        for(u32 j=0;j<value_bytes;++j) bytes[out++]=u8(value>>(j*8));
        i=end;
    }
    return out;
}

inline pqw::Meta user_ids(pqw::Out& o,const UserGroup& g,pqw::StrCol names,std::span<u8> scratch) {
    using namespace pqw;
    if(g.kmax>=names.len()) throw std::out_of_range("user dictionary");
    const auto start=o.pos();
    const auto dict=names.slice(g.kmin,std::size_t(g.kmax)+1);
    string_dict_page(o,dict);
    const auto data=o.pos();const auto n=g.rows;const auto w=bits_for(g.kmax-g.kmin);
    const std::span<const u32> keys(g.keys.data(),n);
    const auto first=encode_key_runs(scratch,keys.first(g.head),g.kmin,w);
    const auto last=encode_key_runs(scratch.subspan(first),keys.subspan(g.head),g.kmin,w);
    const auto tail_packed=hybrid_len(n-g.head,w),packed=hybrid_len(n,w);
    const bool runs=first+std::min(last,tail_packed)<packed,both=last<=tail_packed;
    page(o,0,1+(runs?first+std::min(last,tail_packed):packed),n,8);o.put_le(u8(w));
    if(runs) o.put(scratch.first(first+(both?last:0)));
    if(!runs||!both) {
        const auto a=runs?g.head:0;
        emit_packed(o,n-a,w,[&](std::size_t i){return keys[a+i]-g.kmin;});
    }
    return {6,true,o.pos()-start,data,start,n};
}

inline pqw::Meta revenue_chunk(pqw::Out& o,const UserGroup& g) {
    using namespace pqw;
    const auto start=o.pos();Meta m{1,false,0,start,0,g.rows};
    if(g.head) {
        page(o,2,4,1,0);o.put_le(i32{0});m.dict=true;m.dict_off=start;m.data_off=o.pos();
        page(o,0,varlen(2*g.head)+2,g.head,8);o.put_le(u8{1});o.put_varint(2*g.head);o.put_le(u8{0});
    }
    if(const auto tail=g.rows-g.head) {
        page(o,0,4*tail,tail,0);o.raw({reinterpret_cast<const u8*>(g.revenue.data()+g.head),4*tail});
    }
    m.size=o.pos()-start;return m;
}

inline void nulls(pqw::Out& o,std::size_t n) {
    const auto def=pqw::varlen(2*n)+1;
    pqw::page(o,0,4+def,n,0);o.put_le(u32(def));o.put_varint(2*n);o.put_le(u8{0});
}

inline pqw::Meta category_chunk(pqw::Out& o,const UserGroup& g,pqw::StrCol cats) {
    using namespace pqw;
    const auto start=o.pos();Meta m{6,false,0,start,0,g.rows};
    if(g.defined) {string_dict_page(o,cats);m.dict=true;m.dict_off=start;m.data_off=o.pos();}
    if(g.head) nulls(o,g.head);
    const auto tail=g.rows-g.head;
    if(tail && !g.defined) nulls(o,tail);
    if(g.defined) {
        const auto w=bits_for(cats.len()?cats.len()-1:0);const auto def=hybrid_len(tail,1);
        const auto total=4+def+1+hybrid_len(g.defined,w);
        page(o,0,total,tail,8);
        auto* p=o.tail(total+8);store(p,u32(def));
        auto* levels=varint(p+4,2*((tail+7)/8)+1),*v=p+4+def;
        *v++=u8(w);
        Packer pack(varint(v,2*((g.defined+7)/8)+1),w);
        for(std::size_t i=0;i<tail;i+=64) {
            const auto n=std::min<std::size_t>(tail-i,64);const auto* row=g.category.data()+g.head+i;
            auto mask=valid_mask(row,n);std::memcpy(levels+i/8,&mask,(n+7)/8);
            while(mask) {pack.push(row[std::countr_zero(mask)]);mask&=mask-1;}
        }
        pack.finish((g.defined+7)/8*w);o.commit(total);
    }
    m.size=o.pos()-start;return m;
}

inline auto user_schema() {
    using namespace pqw;
    const StrCol empty{};
    return std::array{Column{"user_id",Data::dict(empty,Keys::u32({}),false,false)},
        Column{"day",Data::date({},IntEnc::Dense)},Column{"sessions",Data::i32({},IntEnc::Dense)},
        Column{"events",Data::i32({},IntEnc::Auto)},Column{"revenue_cents",Data::i32({},IntEnc::Plain)},
        Column{"top_category",Data::dict(empty,Keys::u8({}),true,false)}};
}

class UserFile {
    std::filesystem::path path_,temporary_;
    pqw::File file_;
    pqw::Out out_;
    std::vector<pqw::Group> groups_;
    std::size_t rows_{};
    UserGroup group_;
    std::array<u8,5*USER_LIMIT> runs_;
public:
    explicit UserFile(const std::filesystem::path& p):path_(p),
        temporary_(p.parent_path()/("."+p.filename().native()+".writing")),
        file_(temporary_,O_WRONLY|O_CLOEXEC|O_NOFOLLOW),out_(file_.fd(),1<<20) {out_.put("PAR1");}
    template<class V> void add(const V& v,std::size_t a,std::size_t b,pqw::StrCol cats) {
        const pqw::StrCol names{{v.noff,v.nnoff},{v.names,v.nnames}};
        while(a<b) {
            const auto n=b-a>USER_LIMIT?USER_ROWS:b-a;
            group_.split(v,a,n);
            std::vector<pqw::Meta> columns;columns.reserve(6);
            columns.push_back(user_ids(out_,group_,names,runs_));
            const UserWords* words[]={&group_.day,&group_.sessions,&group_.events};
            for(unsigned j=0;j<3;++j) columns.push_back(range_chunk(out_,{words[j]->data(),n},group_.low[j],group_.high[j],j==2));
            columns.push_back(revenue_chunk(out_,group_));
            columns.push_back(category_chunk(out_,group_,cats));
            groups_.emplace_back(n,std::move(columns));rows_+=n;a+=n;out_.check();
        }
    }
    void finish() {
        const auto before=out_.pos();
        const auto schema=user_schema();pqw::footer(out_,schema,groups_,rows_);
        const auto size=out_.pos()-before;
        if(size>UINT32_MAX) throw std::length_error("user_daily footer");
        out_.put_le(u32(size));out_.put("PAR1");out_.flush();file_.close();
        std::filesystem::rename(temporary_,path_);
    }
};
class WorkerUserOutput {
    std::unique_ptr<UserFile> file_;
public:
    template<class V> void add(const V& v,std::size_t a,std::size_t b,const std::filesystem::path& path,pqw::StrCol cats) {
        if(!file_) file_=std::make_unique<UserFile>(path);
        file_->add(v,a,b,cats);
    }
    void finish() {if(file_) file_->finish();}
};
}
