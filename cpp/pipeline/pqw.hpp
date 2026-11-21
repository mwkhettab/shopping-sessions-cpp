#pragma once
#include "pqw_encoding.hpp"
#include <filesystem>
#include <optional>
#include <sys/mman.h>

namespace pipeline::pqw {
struct Meta {
    i32 type;
    bool dict{};
    u64 size{},data_off{},dict_off{};
    std::size_t nv{};
};
inline u8 *copy_string(u8* dst,const StrCol& s,std::size_t key) {
    const std::size_t start=s.offsets[key],len=s.offsets[key+1]-start;
    store(dst,u32(len));
    auto n=(len<=16 && start+16<=s.data.size())?16:len;
    if(n) std::memcpy(dst+4,s.data.data()+start,n);
    return dst+4+len;
}
inline void plain_strings(Out& o,const StrCol& s) {
    for(std::size_t a=0;a<s.len();a+=BLOCK) {
        const auto b=std::min(s.len(),a+BLOCK),size=4*(b-a)+s.bytes_in(a,b);
        auto *p=o.tail(size);
        for(auto i=a;i<b;++i) p=copy_string(p,s,i);
        o.commit(size);o.check();
    }
}
inline void string_dict_page(Out& o,const StrCol& s) {
    page(o,2,4*s.len()+s.bytes_in(0,s.len()),s.len(),0);
    plain_strings(o,s);
}
inline u64 emit_u8_opt(Out& o,const StrCol& dict,std::span<const u8> keys,const Plan& plan) {
    string_dict_page(o,dict);
    const auto off=o.pos();
    const auto n=keys.size(),def=hybrid_len(n,1),m=plan.m;
    const auto w=plan.w;
    const auto total=4+def+1+hybrid_len(m,w);
    page(o,0,total,n,8);
    auto *p=o.tail(total+8);
    store(p,u32(def));
    auto *q=varint(p+4,2*((n+7)/8)+1);
    auto *r=p+4+def;*r=u8(w);
    Packer pack(varint(r+1,2*((m+7)/8)+1),w);
    for(std::size_t i=0;i<n;i+=64) {
        const auto count=std::min<std::size_t>(64,n-i);
        auto mask=valid_mask(keys.data()+i,count);
        std::memcpy(q+i/8,&mask,(count+7)/8);
        while(mask) {const auto j=std::countr_zero(mask);pack.push(keys[i+j]);mask&=mask-1;}
    }
    pack.finish(((m+7)/8)*w);o.commit(total);o.check();return off;
}
template<class T> std::optional<u64> emit_dict(Out& o,const Data& d,std::span<const T> keys,const Plan& p) {
    constexpr auto null=std::numeric_limits<T>::max();
    const auto n=keys.size();
    if constexpr(std::is_same_v<T,u8>)
        if(d.nullable && !d.plain && p.m) return emit_u8_opt(o,d.strings,keys,p);
    const bool dict=!d.plain && p.m;
    if(dict) string_dict_page(o,d.strings);
    const auto off=o.pos();
    const auto def=d.nullable?4+hybrid_len(n,1):0;
    page(o,0,def+(dict?1+hybrid_len(p.m,p.w):p.vbytes),n,dict?8:0);
    if(d.nullable) {
        o.put_le(u32(def-4));emit_packed(o,n,1,[&](auto i){return u32(keys[i]!=null);});
    }
    if(dict) {
        o.put_le(u8(p.w));
        if(d.nullable) {
            const auto groups=(p.m+7)/8,bytes=groups*p.w;o.put_varint(groups*2+1);
            Packer pack(o.tail(bytes),p.w);
            for(T k:keys) if(k!=null) pack.push(k);
            pack.finish(bytes);o.commit(bytes);o.check();
        } else {
            const auto base=d.keys.base;
            emit_packed(o,n,p.w,[&](auto i){return u32(keys[i])-base;});
        }
        return off;
    }
    if(p.m) {
        auto *dst=o.tail(p.vbytes);
        for(T k:keys) if(!d.nullable||k!=null) dst=copy_string(dst,d.strings,k);
        o.commit(p.vbytes);o.check();
    }
    return {};
}
template<class T> void emit_ints(Out& o,std::span<const T> v,const Plan& p,Meta& m) {
    if(p.form==Plan::Plain) {
        page(o,0,v.size_bytes(),v.size(),0);
        o.raw({reinterpret_cast<const u8*>(v.data()),v.size_bytes()});return;
    }
    page(o,2,p.card*sizeof(T),p.card,0);
    auto *dst=o.tail(p.card*sizeof(T));
    if(p.form==Plan::Hash) for(std::size_t i=0;i<p.card;++i) store(dst+i*sizeof(T),T(p.dict[i]));
    else for(std::size_t i=0;i<p.card;++i) store(dst+i*sizeof(T),T(p.min+i64(i)));
    o.commit(p.card*sizeof(T));
    m.dict=true;m.dict_off=m.data_off;m.data_off=o.pos();
    page(o,0,1+hybrid_len(v.size(),p.w),v.size(),8);o.put_le(u8(p.w));
    if(p.form==Plan::Hash) emit_packed(o,v.size(),p.w,[&](auto i){return p.keys[i];});
    else emit_packed(o,v.size(),p.w,[&](auto i){return u32(i64(v[i])-p.min);});
}
inline Meta emit_chunk(Out& o,const Data& d,const Plan& p) {
    const auto start=o.pos();
    const auto n=d.rows();
    Meta m{d.ptype(),false,0,start,0,n};
    switch(d.form) {
        case Data::Str:page(o,0,4*n+d.strings.bytes_in(0,n),n,0);plain_strings(o,d.strings);break;
        case Data::Dict:{
            auto off=d.keys.visit([&](auto keys){return emit_dict(o,d,keys,p);});
            if(off) {m.dict=true;m.dict_off=start;m.data_off=*off;}
            break;
        }
        case Data::I64:emit_ints(o,d.b,p,m);break;
        default:emit_ints(o,d.a,p,m);break;
    }
    m.size=o.pos()-start;return m;
}
using Group=std::pair<std::size_t,std::vector<Meta>>;
inline void footer(Out& o,std::span<const Column> cols,std::span<const Group> groups,std::size_t n) {
    Thrift t(o);t.integer(1,1);t.list(2,12,cols.size()+1);
    t.elem();t.bin(4,"schema");t.integer(5,i32(cols.size()));t.end();
    for(const auto& c:cols) {
        const auto& d=c.data;
        t.elem();t.integer(1,d.ptype());t.integer(3,d.form==Data::Dict && d.nullable);t.bin(4,c.name);
        if(d.form==Data::Date || d.form==Data::Str || d.form==Data::Dict) {
            const bool date=d.form==Data::Date;
            t.integer(6,date?6:0);t.begin(10);t.begin(date?6:1);t.end();t.end();
        }
        t.end();
    }
    t.large(3,i64(n));t.list(4,12,groups.size());
    for(std::size_t g=0;g<groups.size();++g) {
        const auto& [rows,metas]=groups[g];u64 total=0;
        t.elem();t.list(1,12,metas.size());
        for(std::size_t i=0;i<metas.size();++i) {
            const auto& m=metas[i];const auto name=cols[i].name;
            t.elem();t.large(2,0);t.begin(3);t.integer(1,m.type);t.list(2,5,m.dict?3:2);
            o.put_varint(zz32(0));o.put_varint(zz32(3));
            if(m.dict) o.put_varint(zz32(8));
            t.list(3,8,1);o.put_varint(name.size());o.put(name);t.integer(4,0);
            t.large(5,i64(m.nv));t.large(6,i64(m.size));t.large(7,i64(m.size));t.large(9,i64(m.data_off));
            if(m.dict) t.large(11,i64(m.dict_off));
            t.end();t.end();total+=m.size;
        }
        t.large(2,total);t.large(3,rows);
        const auto first=metas.empty()?4:metas.front().dict?metas.front().dict_off:metas.front().data_off;
        t.large(5,first);t.large(6,total);t.small(7,i16(g));t.end();
    }
    t.bin(6,"pqw version 0.1");t.stop();
}
inline void encode_into(Out& o,std::span<const Column> cols,Opts opts) {
    if(cols.empty()) throw std::invalid_argument("empty parquet schema");
    const auto n=cols.front().data.rows(),rpg=std::max<std::size_t>(1,opts.rows_per_group);
    for(const auto& c:cols) if(c.data.rows()!=n) throw std::invalid_argument("inconsistent parquet column lengths");
    o.put("PAR1");std::vector<Group> groups;
    for(std::size_t a=0;a<n;) {
        const auto b=a+std::min(n-a,rpg);
        std::vector<Data> data;std::vector<Plan> plans;
        data.reserve(cols.size());plans.reserve(cols.size());std::size_t bound=0;
        for(const auto& c:cols) {
            auto d=c.data.narrow_slice(a,b);
            data.push_back(d);plans.push_back(plan(d));
            if(o.fd<0) bound+=chunk_bound(d,plans.back());
        }
        if(o.fd<0) o.reserve(bound+512+160*cols.size());
        std::vector<Meta> metas;metas.reserve(cols.size());
        for(std::size_t j=0;j<cols.size();++j) metas.push_back(emit_chunk(o,data[j],plans[j]));
        groups.emplace_back(b-a,std::move(metas));a=b;
    }
    const auto before=o.pos();
    footer(o,cols,groups,n);
    const auto size=o.pos()-before;
    if(size>UINT32_MAX) throw std::length_error("parquet footer");
    o.put_le(u32(size));o.put("PAR1");o.flush();
}
inline std::vector<u8> encode(std::span<const Column> cols,Opts opts={}) {
    Out o;encode_into(o,cols,opts);return {o.data(),o.data()+o.size()};
}
class File {
    int fd_;
public:
    File(const std::filesystem::path &p,int flags):fd_(::open(p.c_str(),flags|O_CREAT|O_TRUNC,0666)) {
        if(fd_<0) io_error("create parquet");
    }
    ~File(){if(fd_>=0) ::close(fd_);}
    int fd() const {return fd_;}
    void close(){const int f=std::exchange(fd_,-1);if(::close(f)) io_error("close parquet");}
};
inline u64 write_file(const std::filesystem::path &path,std::span<const Column> cols,Opts opts,Method method) {
    if(method.form==Method::Stream) {
        File f(path,O_WRONLY);Out o(f.fd(),method.limit);encode_into(o,cols,opts);f.close();return o.pos();
    }
    Out o;encode_into(o,cols,opts);
    File f(path,method.form==Method::Mmap?O_RDWR:O_WRONLY);
    if(method.form==Method::Whole) write_all(f.fd(),o.data(),o.size());
    else {
        if(::ftruncate(f.fd(),off_t(o.size()))) io_error("resize parquet");
        int flags=MAP_SHARED;
#ifdef MAP_POPULATE
        if(method.populate) flags|=MAP_POPULATE;
#endif
        void* p=::mmap(nullptr,o.size(),PROT_READ|PROT_WRITE,flags,f.fd(),0);
        if(p==MAP_FAILED) io_error("map parquet");
        std::memcpy(p,o.data(),o.size());
        if(::munmap(p,o.size())) io_error("unmap parquet");
    }
    f.close();return o.pos();
}
}
