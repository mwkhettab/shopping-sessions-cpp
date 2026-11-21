#pragma once
#include "pqw_data.hpp"
#include <cerrno>
#include <fcntl.h>
#include <memory>
#include <system_error>
#include <unistd.h>
#if defined(__x86_64__) && (defined(__GNUC__) || defined(__clang__))
#include <immintrin.h>
#endif

namespace pipeline::pqw {
inline constexpr std::size_t BLOCK=8192;
static_assert(std::endian::native==std::endian::little);
inline void io_error(const char *op) {throw std::system_error(errno,std::generic_category(),op);}
inline void write_all(int fd,const u8 *p,std::size_t n) {
    while (n) {
        const auto done=::write(fd,p,n);
        if(done<0 && errno==EINTR) continue;
        if(done<0) io_error("write parquet");
        if(!done) throw std::runtime_error("zero-length parquet write");
        p+=done;n-=std::size_t(done);
    }
}
template<class T> inline void store(u8 *p,T value) {std::memcpy(p,&value,sizeof(T));}
inline u8* varint(u8* dst,u64 v) {
    while(v>=128) {*dst++=u8(v)|128;v>>=7;}
    *dst++=u8(v);return dst;
}
class Out {
    std::unique_ptr<u8[]> mem_;
    std::size_t used_{},cap_{},limit_=SIZE_MAX;
    u64 flushed_{};
public:
    int fd=-1;
    Out()=default;
    Out(int f,std::size_t limit):limit_(limit),fd(f){reserve(limit+BLOCK*64);}
    const u8 *data() const {return mem_.get();}
    std::size_t size() const {return used_;}
    u64 pos() const {return flushed_+used_;}
    void reserve(std::size_t extra) {
        if(extra>SIZE_MAX-used_-32) throw std::length_error("parquet buffer");
        const auto need=used_+extra+32;
        if(need<=cap_) return;
        const auto next=std::max(need,cap_>SIZE_MAX/2?SIZE_MAX:cap_*2);
        auto mem=std::make_unique_for_overwrite<u8[]>(next);
        if(used_) std::memcpy(mem.get(),mem_.get(),used_);
        mem_=std::move(mem);cap_=next;
    }
    u8 *tail(std::size_t bytes) {reserve(bytes);return mem_.get()+used_;}
    void commit(std::size_t n) {used_+=n;}
    void put(std::span<const u8> bytes) {
        if(bytes.empty()) return;
        std::memcpy(tail(bytes.size()),bytes.data(),bytes.size());used_+=bytes.size();
    }
    void put(std::string_view s) {put({reinterpret_cast<const u8*>(s.data()),s.size()});}
    template<class T> void put_le(T v) {store(tail(sizeof(T)),v);commit(sizeof(T));}
    void put_varint(u64 value) {auto *p=tail(10);commit(std::size_t(varint(p,value)-p));}
    void flush() {if(fd>=0 && used_) {write_all(fd,data(),used_);flushed_+=used_;used_=0;}}
    void check() {if(fd>=0 && used_>=limit_) flush();}
    void raw(std::span<const u8> bytes) {
        if(fd>=0 && bytes.size()>=limit_) {
            flush();write_all(fd,bytes.data(),bytes.size());flushed_+=bytes.size();
        } else {put(bytes);check();}
    }
};
inline u32 zz32(i32 v) {return (u32(v)<<1)^(u32(0)-u32(v<0));}
inline u64 zz64(i64 v) {return (u64(v)<<1)^(u64(0)-u64(v<0));}
class Thrift {
    Out& out_;
    std::array<i16,16> stack_{};
    std::size_t depth_{};
    i16 last_{};
    void field(i16 id,u8 type) {
        const int delta=id-last_;
        if(delta>0 && delta<=15) out_.put_le(u8(delta*16+type));
        else {out_.put_le(type);out_.put_varint(zz32(id));}
        last_=id;
    }
public:
    explicit Thrift(Out& out):out_(out){}
    void small(i16 id,i16 v){field(id,4);out_.put_varint(zz32(v));}
    void integer(i16 id,i32 v){field(id,5);out_.put_varint(zz32(v));}
    void large(i16 id,i64 v){field(id,6);out_.put_varint(zz64(v));}
    void bin(i16 id,std::string_view s){field(id,8);out_.put_varint(s.size());out_.put(s);}
    void elem(){stack_[depth_++]=last_;last_=0;}
    void begin(i16 id){field(id,12);elem();}
    void end(){out_.put_le(u8(0));last_=stack_[--depth_];}
    void list(i16 id,u8 type,std::size_t n){
        field(id,9);
        if(n<15) out_.put_le(u8(n*16+type));
        else {out_.put_le(u8(0xf0|type));out_.put_varint(n);}
    }
    void stop(){out_.put_le(u8(0));}
};
inline void page(Out &o,i32 kind,std::size_t size,std::size_t nv,i32 enc) {
    if(size>INT32_MAX || nv>INT32_MAX) throw std::length_error("parquet page");
    Thrift t(o);
    t.integer(1,kind);t.integer(2,i32(size));t.integer(3,i32(size));
    if(!kind) {
        t.begin(5);t.integer(1,i32(nv));t.integer(2,enc);t.integer(3,3);t.integer(4,3);
    } else {
        t.begin(7);t.integer(1,i32(nv));t.integer(2,0);
    }
    t.end();t.stop();
}
template<unsigned W> inline void pack_group(u8 *p,const std::array<u32,8>& v) {
    using wide=__uint128_t;
    if constexpr(W<=8) {
        u64 x=0;for(unsigned k=0;k<8;++k) x|=u64(v[k])<<(k*W);
        store(p,x);
    } else if constexpr(W<=16) {
        wide x=0;for(unsigned k=0;k<8;++k) x|=wide(v[k])<<(k*W);
        store(p,x);
    } else {
        wide lo=0,hi=0;
        for(unsigned k=0;k<4;++k) {lo|=wide(v[k])<<(k*W);hi|=wide(v[k+4])<<(k*W);}
        if constexpr(W==32) {store(p,lo);store(p+16,hi);}
        else {store(p,lo|(hi<<(4*W)));store(p+16,hi>>(128-4*W));}
    }
}
template<unsigned W,class F> void pack_w(u8 *p,std::size_t a,std::size_t b,const F& get) {
    std::size_t i=a;
    for(;i+8<=b;i+=8,p+=W)
        pack_group<W>(p,{get(i),get(i+1),get(i+2),get(i+3),get(i+4),get(i+5),get(i+6),get(i+7)});
    if(i<b) {
        std::array<u32,8> v{};
        for(std::size_t j=0;i+j<b;++j) v[j]=get(i+j);
        pack_group<W>(p,v);
    }
}
template<class F> void emit_packed(Out& o,std::size_t n,u32 w,const F &get) {
    if(!n) return;
    o.put_varint(((n+7)/8)*2+1);
    using Fn=void(*)(u8*,std::size_t,std::size_t,const F&);
    static constexpr auto table=[]<std::size_t... I>(std::index_sequence<I...>){
        return std::array<Fn,32>{&pack_w<I+1,F>...};
    }(std::make_index_sequence<32>{});
    const auto pack=table.at(w-1);
    for(std::size_t a=0;a<n;a+=BLOCK) {
        const auto b=std::min(a+BLOCK,n),size=(b-a+7)/8*w;
        pack(o.tail(size),a,b,get);o.commit(size);o.check();
    }
}
struct Packer {
    u8 *p,*start;
    u64 acc{};
    u32 nb{},w;
    Packer(u8 *dst,u32 width):p(dst),start(dst),w(width){}
    void push(u32 v) {
        acc|=u64(v)<<nb;nb+=w;
        if(nb>=32) {store(p,u32(acc));p+=4;acc>>=32;nb-=32;}
    }
    void finish(std::size_t size) {
        const auto rem=size-std::size_t(p-start);
        std::memset(p,0,rem);std::memcpy(p,&acc,std::min<std::size_t>(rem,8));
    }
};
inline u64 valid_scalar(const u8 *p,std::size_t n) {
    u64 mask=0;for(std::size_t i=0;i<n;++i) mask|=u64(p[i]!=255)<<i;
    return mask;
}
#if defined(__x86_64__) && (defined(__GNUC__) || defined(__clang__))
[[gnu::target("avx512bw,avx512f")]] inline u64 valid_wide(const u8 *p,std::size_t n) {
    const u64 m=n==64?UINT64_MAX:(u64{1}<<n)-1;
    return _mm512_mask_cmpneq_epi8_mask(m,_mm512_maskz_loadu_epi8(m,p),_mm512_set1_epi8(-1));
}
#endif
inline u64 valid_mask(const u8 *p,std::size_t n) {
#if defined(__x86_64__) && (defined(__GNUC__) || defined(__clang__))
    if(__builtin_cpu_supports("avx512bw")) return valid_wide(p,n);
#endif
    return valid_scalar(p,n);
}
}
