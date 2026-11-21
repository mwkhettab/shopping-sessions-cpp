#pragma once
#include "setup_lookups.hpp"
#include <future>

namespace pipeline::setup {
struct Input {Kind kind;Path path;std::size_t len;};
struct Chain {u8* region;std::vector<FileInfo> files;std::vector<Chunk> queue;std::array<std::size_t,3> bytes{};};
struct Keys {u64 user,event,click,ccap;};
inline std::vector<Input> all_files(const Path& data){
    std::vector<Input> files;const std::array<const char*,3> dirs{"events","clicks","links"};
    for(std::size_t k=0;k<dirs.size();++k){std::error_code err;std::filesystem::directory_iterator it(data/dirs[k],err),end;if(err)sys::fallback("cannot list an input folder");
        for(;it!=end;it.increment(err)){if(err)sys::fallback("cannot list an input folder");const auto& p=it->path();if(!p.filename().native().ends_with(".csv"))continue;
            std::error_code e;if(!it->is_regular_file(e)||e)continue;auto n=it->file_size(e);if(!e && n)files.push_back({Kind(k),p,std::size_t(n)});}}
    std::sort(files.begin(),files.end(),[](const Input& a,const Input& b){return a.len>b.len || (a.len==b.len && a.path<b.path);});return files;
}
inline std::pair<std::size_t,std::size_t> part_counts(std::size_t ev_bytes){
    auto e=(ev_bytes/64+4096+BSIZE-1)&~(BSIZE-1);auto u=std::min<std::size_t>((e/16+4096+UBSIZE-1)&~(UBSIZE-1),1u<<26);return {e>>BB,u>>UB};
}
inline u32 ckix_mode(){return u32(parse::env_size("XL_CKIX",0));}
inline u64 click_cap(std::size_t ckbytes){auto n=(ckbytes/20+4096+63)&~std::size_t{63};return n>=(1u<<31)||!ckix_mode()?0:n;}
inline std::size_t csr_cap(u64 ccap,std::size_t ck,std::size_t lk){return (ccap?std::size_t(ccap):ck/10+lk/6)+64;}
inline Bytes sample_bytes(Bytes text){auto b=body(text);if(b.size()<=600000)return b;auto cut=b.substr(0,600000).rfind('\n');return cut==b.npos?b.substr(0,0):b.substr(0,cut+1);}
inline Bytes file_view(const Chain& c,const FileInfo& f){return {reinterpret_cast<const char*>(c.region+f.off),f.len};}
template<class OnMap> Chain map_inputs(const std::vector<Input>& all,bool parallel,OnMap&& mapped){
    constexpr std::size_t hp=2u<<20,pg=4096;auto padded=[](std::size_t n){return (n+pg+hp-1)&~(hp-1);};
    std::size_t total=2*hp;for(const auto& f:all){if(f.len>=(u64{1}<<40))sys::fallback("input too large");total+=padded(f.len);if(total>=(u64{1}<<40))sys::fallback("input too large");}
    auto p=::mmap(nullptr,total+hp,PROT_READ,sys::anonymous_flags,-1,0);if(p==MAP_FAILED)sys::fallback("mmap region");
    Chain chain{reinterpret_cast<u8*>((std::uintptr_t(p)+hp-1)&~(hp-1)),{},{},{}};chain.files.reserve(all.size());std::size_t off=hp;
    if(parallel)parse::fds_init(all.size());
    for(std::size_t i=0;i<all.size();++i){const auto& f=all[i];int fd=::open(f.path.c_str(),O_RDONLY);if(fd<0)sys::fallback("open input");
        if(::mmap(chain.region+off,f.len,PROT_READ,MAP_PRIVATE|MAP_FIXED,fd,0)==MAP_FAILED)sys::fallback("mmap input");
        chain.bytes[std::size_t(f.kind)]+=f.len;chain.files.push_back({f.kind,off,f.len,SIZE_MAX});if(parallel)parse::set_fd(i,fd);else parse::keep_fd(fd);
        mapped(f.kind,file_view(chain,chain.files.back()));off+=padded(f.len);
    }
    sys::lap("s.mmap");return chain;
}
inline void make_queue(Chain& c,std::size_t nt){auto size=std::clamp<std::size_t>((c.bytes[0]+c.bytes[1]+c.bytes[2])/(nt*16),64u<<10,2u<<20);
    for(std::size_t i=0;i<c.files.size();++i){auto len=c.files[i].len;for(std::size_t a=1;a<len;){auto b=std::min(a+size,len);c.queue.push_back({u32(i),a,b});a=b;}}}
inline std::pair<u64,u64> event_keys(const keyrec::Sample& sample,i64 ws,keyrec::Stats& st){
    auto u=keyrec::recover_user_key(sample,st);if(!u)sys::fallback("no user key");auto e=keyrec::recover_event_key(sample,*u,ws,st);if(!e)sys::fallback("no event key");return {*u,*e};
}
inline std::pair<u64,u64> click_key(std::vector<keyrec::ClickRow> rows,u64 ukey,std::size_t bytes,keyrec::Stats& st){
    auto cap=click_cap(bytes);if(!cap)return {0,0};keyrec::Sample sample;sample.clicks=std::move(rows);auto k=keyrec::recover_click_key_lim(sample,ukey,cap,st);return k?std::pair{*k,cap}:std::pair<u64,u64>{0,0};
}
inline std::size_t row_capacity(std::size_t bytes,std::size_t nt){return bytes/40/nt*2+65536;}
inline std::unique_ptr<G> make_model(std::size_t nt,bool check,i64 ws,i64 we,Keys keys,Chain c,Products products,Campaigns campaigns){
    auto g=std::make_unique<G>();g->nt=nt;g->check=check;g->ws=ws;g->wlen=u64(we-ws);g->cbase=ws-7*DAY_MS;g->clen=g->wlen+7*DAY_MS;g->ing_base=ws-DAY_MS;
    g->ws_mod=u64(ws%DAY_MS);g->day0=i32(ws/DAY_MS);g->nd=std::size_t((g->wlen+g->ws_mod+DAY_MS-1)/DAY_MS);
    g->ukey=keys.user;g->ekey=keys.event;g->ckey=keys.click;g->ccap=keys.ccap;g->cpart=ckix_mode()==2;
    auto [ev,ck,lk]=c.bytes;auto [nb,nub]=part_counts(ev);g->nb=nb;g->nub=nub;g->ecap=nb<<BB;g->ucap=nub<<UB;g->ev_bytes=ev;g->row_cap=row_capacity(ev,nt);
    g->ev_arena=sys::Arena(ev/28*16+nt*nb*4096+nt*sys::HP+(16u<<20));g->us_arena=sys::Arena((ck/10+lk/6)*16+nt*nub*4096+nt*sys::HP+(16u<<20));
    g->prod=std::move(products.prod);g->cats=std::move(products.cats);g->camp=std::move(campaigns.camp);g->channels=std::move(campaigns.channels);g->nc=campaigns.nc;
    g->region=c.region;g->files=std::move(c.files);g->queue=std::move(c.queue);g->workers.reserve(nt);g->csr_cap=csr_cap(g->ccap,ck,lk);
    g->utab=reinterpret_cast<u16*>(sys::anon(g->ucap*2));g->claim=reinterpret_cast<std::atomic<u64>*>(sys::anon(g->ucap/8+64));
    // Establish atomic object lifetimes explicitly in the zero-filled claim bitmap.
    std::uninitialized_value_construct_n(g->claim,g->ucap/64+8);
    g->csr_off=reinterpret_cast<u32*>(sys::anon((g->ucap+nub+8)*4));g->csr=reinterpret_cast<links::Click*>(sys::anon(g->csr_cap*sizeof(links::Click)));
    g->edges=reinterpret_cast<EdgeInfo*>(sys::anon(nb*sizeof(EdgeInfo)));
    if(check)std::fprintf(stderr,"[pipeline] user key %llx, event key %llx, click key %llx, click capacity %llu\n",(unsigned long long)keys.user,(unsigned long long)keys.event,(unsigned long long)keys.click,(unsigned long long)keys.ccap);
    return g;
}
inline std::unique_ptr<G> setup_serial(const Path& data,std::size_t nt,bool check){
    auto [ws,we]=window(data);sys::WORKERS.store(true);
    std::pair<Products,Campaigns> lookup;std::thread worker([&]{lookup.first=products_serial(body(read(data/"products.csv")));lookup.second=campaigns(data,false);});
    auto all=all_files(data);sys::lap("s.list");auto c=map_inputs(all,false,[](Kind,Bytes){});make_queue(c,nt);
    keyrec::Sample sample;for(const auto& f:c.files)if(f.kind==Kind::Ev&&sample.events.size()<5000)sample.add_event_lines(sample_bytes(file_view(c,f)),5000);
    sys::lap("s.sample");keyrec::Stats stats;auto [uk,ek]=event_keys(sample,ws,stats);
    keyrec::Sample clicks;if(click_cap(c.bytes[1]))for(const auto& f:c.files)if(f.kind==Kind::Ck){clicks.add_click_lines(sample_bytes(file_view(c,f)),4000);break;}
    auto [ck,cap]=click_key(std::move(clicks.clicks),uk,c.bytes[1],stats);sys::lap("maps+keys");worker.join();sys::lap("lookups");
    auto g=make_model(nt,check,ws,we,{uk,ek,ck,cap},std::move(c),std::move(lookup.first),std::move(lookup.second));
    sys::ANON_TINY.store(true);for(std::size_t i=0;i<nt;++i){auto t=std::make_unique<TState>();t->core=core::Core(0,1,16,0,0);g->workers.push_back(std::move(t));}sys::ANON_TINY.store(false);
    return g;
}
template<class T> struct Slot {
    std::optional<T> value;std::atomic_bool done{};
    void set(T v){value.emplace(std::move(v));done.store(true,std::memory_order_release);}
    T& get(){while(!done.load(std::memory_order_acquire))sys::spin_loop();return *value;}
    T take(){auto t=std::move(get());value.reset();return t;}
};
inline std::unique_ptr<G> setup_par(std::string_view dir,std::size_t nt,bool check){
    const Path data(dir);if(nt<2)return setup_serial(data,nt,check);
    const auto [ws,we]=window(data);sys::WORKERS.store(true);auto ws_mod=u64(ws%DAY_MS);auto nd=std::size_t((we-ws+i64(ws_mod)+DAY_MS-1)/DAY_MS);auto day0=i32(ws/DAY_MS);
    const auto configured=sys::SETUP_KP.load(std::memory_order_relaxed);const auto kp=configured?std::clamp<std::size_t>(configured,1,64):std::clamp<std::size_t>(2*nt,2,16),ks=std::clamp<std::size_t>(nt,2,8);
    Slot<Campaigns> camps;Slot<Bytes> ptxt,sbytes,cbytes;Slot<Products> lookups;Slot<std::size_t> rowcap;Slot<std::pair<std::size_t,std::size_t>> nbs;
    std::vector<Slot<std::optional<ProdPart>>> pparts(kp);std::vector<Slot<core::Core>> cores(nt);std::vector<Slot<keyrec::Sample>> spieces(ks);
    Slot<std::vector<keyrec::ClickRow>> crows;std::atomic<std::size_t> snext{},qnext{};std::atomic_bool ctaken{};
    auto click_sample=[&]{if(cbytes.done.load(std::memory_order_acquire) && !ctaken.exchange(true,std::memory_order_acq_rel)){keyrec::Sample s;s.add_click_lines(cbytes.get(),4000);crows.set(std::move(s.clicks));}};
    auto sample_piece=[&](auto j){keyrec::Sample sample;sample.add_event_lines(piece(sbytes.get(),j,ks),5000);spieces[j].set(std::move(sample));};
    const auto upart=sys::SETUP_PARTS.load(std::memory_order_relaxed);const auto q_ord=2+kp,q_merge=3+kp,q_part=4+kp,q_core=q_part+(upart?1:0),nq=q_core+nt;
    std::vector<Slot<std::pair<parse::Part,parse::Part>>> parts(upart?nt:0);
    auto task=[&](std::size_t i){if(i==1)camps.set(campaigns(data));else if(i==0)ptxt.set(map_file(data/"products.csv"));
        else if(i<q_ord)pparts[i-2].set(products_part(piece(body(ptxt.get()),i-2,kp)));
        else if(i==q_ord){if(parse::pin_mode())(void)parse::cpu_order();}
        else if(i>=q_core){const auto nc=camps.get().nc;cores[i-q_core].set(core::Core(nc,nd,rowcap.get(),ws_mod,day0));}
        else if(i==q_part){auto [nb,nub]=nbs.get();for(auto& p:parts)p.set(std::pair{parse::Part::new_unbound(nb),parse::Part::new_unbound(nub)});}
        else if(i==q_merge){std::vector<std::optional<ProdPart>> pv;pv.reserve(kp);for(auto& s:pparts)pv.push_back(s.take());auto prod=products_merge(pv);auto& camp=camps.get().camp;
            parse::init_tabs_shared(prod.prod.data(),prod.prod.size(),camp.data(),camp.size());lookups.set(std::move(prod));}};
    auto help=[&]{click_sample();if(sbytes.done.load(std::memory_order_acquire)){for(;;){auto j=snext.fetch_add(1,std::memory_order_relaxed);if(j>=ks)break;sample_piece(j);}}
        auto i=qnext.fetch_add(1,std::memory_order_relaxed);if(i<nq){task(i);return true;}return false;};
    Slot<Chain> chain;Slot<Keys> keys;
    sys::run_par(nt,[&](std::size_t tid){if(tid){while(help()){};while(!(sbytes.done.load(std::memory_order_acquire)&&cbytes.done.load(std::memory_order_acquire))&&!keys.done.load(std::memory_order_acquire))sys::spin_loop();while(help()){};return;}
        auto all=all_files(data);std::size_t evbytes=0;for(const auto& f:all)if(f.kind==Kind::Ev)evbytes+=f.len;rowcap.set(row_capacity(evbytes,nt));nbs.set(part_counts(evbytes));sys::lap("s.list");
        const bool ix=ckix_mode()!=0;auto c=map_inputs(all,true,[&](Kind k,Bytes file){if(k==Kind::Ev&&!sbytes.done.load(std::memory_order_relaxed))sbytes.set(sample_bytes(file));if(k==Kind::Ck&&!cbytes.done.load(std::memory_order_relaxed))cbytes.set(ix?sample_bytes(file):Bytes{});});
        if(!sbytes.done.load(std::memory_order_relaxed))sbytes.set({});if(!cbytes.done.load(std::memory_order_relaxed))cbytes.set({});
        for(;;){auto j=snext.fetch_add(1,std::memory_order_relaxed);if(j>=ks)break;sample_piece(j);}
        keyrec::Sample sample;for(auto& p:spieces){auto v=p.take();sample.events.insert(sample.events.end(),v.events.begin(),v.events.end());}if(sample.events.size()>5000)sample.events.resize(5000);
        bool first=true;for(const auto& f:c.files)if(f.kind==Kind::Ev){if(!std::exchange(first,false)&&sample.events.size()<5000)sample.add_event_lines(sample_bytes(file_view(c,f)),5000);}
        sys::lap("s.sample");keyrec::Stats stats;auto [uk,ek]=event_keys(sample,ws,stats);click_sample();auto [ck,cap]=click_key(crows.take(),uk,c.bytes[1],stats);
        keys.set({uk,ek,ck,cap});sys::lap("maps+keys");make_queue(c,nt);chain.set(std::move(c));while(help()){};
    });sys::lap("lookups");
    auto g=make_model(nt,check,ws,we,keys.take(),chain.take(),lookups.take(),camps.take());for(auto& c:cores){auto t=std::make_unique<TState>();t->core=c.take();g->workers.push_back(std::move(t));}
    parse::CORE_READY.store(true,std::memory_order_relaxed);if(upart && nbs.get()==std::pair{g->nb,g->nub}){for(std::size_t i=0;i<nt;++i){auto [ev,us]=parts[i].take();g->t(i).ev=std::move(ev);g->t(i).us=std::move(us);}parse::PARTS_READY.store(true,std::memory_order_relaxed);}return g;
}
inline std::unique_ptr<G> setup(std::string_view dir,std::size_t nt,bool check){const char* v=std::getenv("XL_PSETUP");if(nt>=2 && sys::pool_size()+1>=nt && (!v||std::string_view(v)=="1"))return setup_par(dir,nt,check);return setup_serial(Path(dir),nt,check);}
}
