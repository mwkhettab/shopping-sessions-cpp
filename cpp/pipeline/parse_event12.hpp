#pragma once
#include "parse_part.hpp"
#include <concepts>

namespace pipeline::parse {
#ifndef SHOP_PACKED_EVENTS
#define SHOP_PACKED_EVENTS 1
#endif
inline constexpr bool PACKED_EVENTS=SHOP_PACKED_EVENTS!=0;
inline constexpr u64 R12_MASK=(u64{1}<<59)-1;
inline constexpr u64 R12_USER_LIMIT=u64{1}<<22;
inline constexpr std::size_t R12_BYTES=12,R12_STAGE=192;
static_assert(BB<=15 && NALIAS<=16 && LINK_OFF%R12_STAGE==0);

inline Rec unpack_event12(const u8* p) {
    u64 q;u32 h;
    std::memcpy(&q,p,8);std::memcpy(&h,p+8,4);
    return {(u64(h)<<32)|(q>>59),q&R12_MASK};
}

class EvPart {
    struct alignas(64) Stage {std::array<u8,R12_STAGE> bytes;};
    struct alignas(64) Page {u8 data[LINK_OFF];Page* next;};
    static constexpr std::size_t esc_size=1024,esc_bytes=1008;
    struct alignas(64) Esc {Rec rows[esc_bytes/sizeof(Rec)];Esc* next;};
    static_assert(sizeof(Page)==CHUNK && sizeof(Esc)==esc_size);
    std::vector<Stage> stages_;
    std::vector<u16> fill_;
    std::vector<Page*> head_;
    std::vector<std::uintptr_t> cur_;
    std::vector<u32> pages_,counts_;
    std::vector<Esc*> escapes_;
    std::vector<std::uintptr_t> esc_cur_;
    std::vector<u8> sink_storage_;
    u8* sink_page_{};
    u8* block_{};
    u8* block_end_{};
    u8* esc_next_{};
    void* arena_{};
    u8* (*alloc_)(void*,std::size_t){};
    bool populate_{};
    std::array<u64,NB+8> pending_{};
    std::array<std::array<u64,3>,NB> slow_{};
    std::size_t nslow_{};

    explicit EvPart(std::size_t n)
        :stages_(n+1),fill_(n+1),head_(n),cur_(n+1,LINK_OFF),pages_(n),counts_(n),
         escapes_(n),esc_cur_(n,esc_bytes),sink_storage_(2*CHUNK),populate_(popw()) {
        void* p=sink_storage_.data();auto space=sink_storage_.size();
        sink_page_=static_cast<u8*>(std::align(CHUNK,CHUNK,p,space));
    }
    [[gnu::noinline]] u8* take4k() {
        if(block_==block_end_) {
            block_=alloc_(arena_,BLOCK);block_end_=block_+BLOCK;
#ifdef __linux__
            if(populate_) ::madvise(block_,BLOCK,23);
#endif
        }
        auto* out=block_;block_+=CHUNK;return out;
    }
    [[gnu::noinline]] std::uintptr_t next_page(std::size_t b,std::uintptr_t old) {
        if(b==head_.size()) return reinterpret_cast<std::uintptr_t>(sink_page_);
        auto* next=::new(take4k()) Page;
        if(old==LINK_OFF) head_[b]=next;
        else reinterpret_cast<Page*>(old-LINK_OFF)->next=next;
        ++pages_[b];
        return reinterpret_cast<std::uintptr_t>(next);
    }
    [[gnu::noinline]] std::uintptr_t next_escape(std::size_t b,std::uintptr_t old) {
        if((reinterpret_cast<std::uintptr_t>(esc_next_)&(CHUNK-1))==0) esc_next_=take4k();
        auto* next=::new(esc_next_) Esc;esc_next_+=esc_size;
        if(old==esc_bytes) escapes_[b]=next;
        else reinterpret_cast<Esc*>(old-esc_bytes)->next=next;
        return reinterpret_cast<std::uintptr_t>(next);
    }
    std::uintptr_t destination(std::size_t b,std::size_t bytes) {
        auto p=cur_[b];
        if((p&(CHUNK-1))==LINK_OFF) p=next_page(b,p);
        cur_[b]=p+bytes;fill_[b]=0;return p;
    }
#if defined(__x86_64__) && (defined(__GNUC__) || defined(__clang__))
    template<bool Stream> __attribute__((target("avx512f")))
    void flush_native(std::size_t n) {
        for(std::size_t i=0;i<n;++i) {
            const auto b=pending_[i];
            auto* dst=reinterpret_cast<__m512i*>(destination(b,R12_STAGE));
            auto* src=reinterpret_cast<const __m512i*>(stages_[b].bytes.data());
            for(std::size_t j=0;j<R12_STAGE/64;++j) {
                const auto value=_mm512_load_si512(src+j);
                if constexpr(Stream) _mm512_stream_si512(dst+j,value);
                else _mm512_store_si512(dst+j,value);
            }
        }
    }
#endif
    [[gnu::noinline]] void flush(std::size_t n) {
#if defined(__x86_64__) && (defined(__GNUC__) || defined(__clang__))
        if(__builtin_cpu_supports("avx512f")) {
            if(NT) flush_native<true>(n);else flush_native<false>(n);
            return;
        }
#endif
        for(std::size_t i=0;i<n;++i) {
            auto b=pending_[i];
            std::memcpy(reinterpret_cast<void*>(destination(b,R12_STAGE)),stages_[b].bytes.data(),R12_STAGE);
        }
    }
    template<class Store> inline void batch(std::size_t n,const u64* bs,const u64* lp,
                                            const std::array<u64,2>* records,Store store) {
        std::size_t pending=0;
        for(std::size_t i=0;i<n;++i) {
            const auto b=bs[i];auto f=fill_[b];
            if(f==R12_STAGE) {flush(pending);pending=0;f=0;}
            store(reinterpret_cast<u8*>(std::uintptr_t(lp[i]))+f,records+i);
            f+=R12_BYTES;fill_[b]=f;
            pending_[pending]=b;pending+=f==R12_STAGE;
        }
        if(pending) flush(pending);
    }
#if defined(__x86_64__) && (defined(__GNUC__) || defined(__clang__))
    __attribute__((target("avx512f,avx512bw,avx512vl")))
    void native_batch(std::size_t n,const u64* bs,const u64* lp,const std::array<u64,2>* records) {
        batch(n,bs,lp,records,[](u8* out,const auto* rec) __attribute__((target("avx512f,avx512bw,avx512vl"))) {
            _mm_mask_storeu_epi8(out,0xfff,_mm_load_si128(reinterpret_cast<const __m128i*>(rec)));
        });
    }
#endif
public:
    EvPart()=default;
    EvPart(const EvPart&)=delete;
    EvPart& operator=(const EvPart&)=delete;
    EvPart(EvPart&&)=default;
    EvPart& operator=(EvPart&&)=default;
    static EvPart new_unbound(std::size_t n) {return EvPart(n);}
    template<class Arena> void set_arena(Arena& arena) {
        arena_=&arena;alloc_=[](void* p,std::size_t n){return static_cast<Arena*>(p)->alloc(n);};
    }
    template<class Arena> EvPart(Arena& arena,std::size_t n):EvPart(n) {set_arena(arena);}
    const u8* lines_ptr() const {return reinterpret_cast<const u8*>(stages_.data());}
    std::size_t sink_bucket() const {return head_.size();}
    void push(std::size_t b,u64 w0,u64 w1) {
        auto p=esc_cur_[b];
        if((p&(esc_size-1))==esc_bytes) p=next_escape(b,p);
        *reinterpret_cast<Rec*>(p)={w0,w1};esc_cur_[b]=p+sizeof(Rec);
    }
    void defer(std::size_t b,u64 w0,u64 w1) {
        slow_[nslow_++]={b,w0,w1};
    }
    void flush_escapes() {
        for(std::size_t i=0;i<nslow_;++i){const auto& r=slow_[i];push(r[0],r[1],r[2]);}
        nslow_=0;
    }
    void push_encoded(std::size_t n,const u64* bs,const u64* lp,const std::array<u64,2>* records) {
#if defined(__x86_64__) && (defined(__GNUC__) || defined(__clang__))
        if(__builtin_cpu_supports("avx512bw") && __builtin_cpu_supports("avx512vl")) {
            native_batch(n,bs,lp,records);return;
        }
#endif
        batch(n,bs,lp,records,[](u8* dst,const auto* row){std::memcpy(dst,row,R12_BYTES);});
    }
    void finish() {
        for(std::size_t b=0;b<head_.size();++b) {
            const auto n=fill_[b];
            if(n) std::memcpy(reinterpret_cast<void*>(destination(b,n)),stages_[b].bytes.data(),n);
            if(head_[b]) {
                auto last=(cur_[b]-1)&~std::uintptr_t(CHUNK-1);
                counts_[b]=(pages_[b]-1)*u32(LINK_OFF/R12_BYTES)+u32((cur_[b]-last)/R12_BYTES);
            }
        }
#if defined(__x86_64__) || defined(_M_X64)
        if(NT) _mm_sfence();
#endif
    }
    template<class F> requires std::invocable<F&,const u8*,std::size_t,const i8*>
    void for_each_chunk(std::size_t b,F&& f) const {
        if(b>=head_.size() || !head_[b]) return;
        const auto last=(cur_[b]-1)&~std::uintptr_t(CHUNK-1);
        auto* p=head_[b];
        while(reinterpret_cast<std::uintptr_t>(p)!=last) {
            auto* next=p->next;f(p->data,LINK_OFF/R12_BYTES,reinterpret_cast<const i8*>(next));p=next;
        }
        f(p->data,(cur_[b]-last)/R12_BYTES,nullptr);
    }
    template<class F> void for_each_esc(std::size_t b,F&& f) const {
        if(b>=escapes_.size() || !escapes_[b]) return;
        auto* p=escapes_[b];
        const auto last=(esc_cur_[b]-1)&~std::uintptr_t(esc_size-1);
        while(reinterpret_cast<std::uintptr_t>(p)!=last) {
            for(const auto& row:p->rows) f(row);
            p=p->next;
        }
        for(std::size_t i=0;i<(esc_cur_[b]-last)/sizeof(Rec);++i) f(p->rows[i]);
    }
    template<class F> void for_each_rec(std::size_t b,F&& f) const {
        for_each_chunk(b,[&](const u8* p,std::size_t n,const i8*){
            for(std::size_t i=0;i<n;++i) f(unpack_event12(p+i*R12_BYTES));
        });
        for_each_esc(b,f);
    }
    // The portable stage can consume bounded synchronous spans as before.
    template<class F> void for_each(std::size_t b,F&& f) const {
        std::array<Rec,64> out;std::size_t n=0;
        for_each_rec(b,[&](Rec r){
            out[n++]=r;
            if(n==out.size()) {f(std::span<const Rec>(out));n=0;}
        });
        if(n) f(std::span<const Rec>(out.data(),n));
    }
    std::size_t count(std::size_t b) const {return counts_[b];}
};
using EventPart=std::conditional_t<PACKED_EVENTS,EvPart,Part>;
}
