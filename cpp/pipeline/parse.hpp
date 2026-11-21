#pragma once
#include "k_free.hpp"
#include "links.hpp"
#include "parse_links.hpp"
#include <charconv>
#include <fstream>
#include <numeric>
#include <string>
#include <unistd.h>

namespace pipeline::parse {
inline std::size_t env_size(const char* name,std::size_t def){
    auto s=std::getenv(name);if(!s)return def;std::string_view v=s;
    while(!v.empty() && (v.front()==' '||v.front()=='\t'))v.remove_prefix(1);
    while(!v.empty() && (v.back()==' '||v.back()=='\t'))v.remove_suffix(1);
    std::size_t n{};auto [p,e]=std::from_chars(v.data(),v.data()+v.size(),n);return e==std::errc{} && p==v.data()+v.size() ? n : def;
}
struct Drv {std::size_t rd;u32 pin,qord;std::size_t ch;bool st2q;};
inline const Drv& drv() {
    static const Drv d{env_size("XL_RD",0)<<10,u32(env_size("XL_PIN",2)),u32(env_size("XL_QORD",2)),env_size("XL_CH",8192)<<10,env_size("XL_ST2Q",1)==1};return d;
}
inline u32 pin_mode(){return drv().pin;}
inline std::vector<int> FDS;
inline void keep_fd(int fd){if(drv().rd && std::count_if(FDS.begin(),FDS.end(),[](auto x){return x>=0;})<40)FDS.push_back(fd);else{if(fd>=0)::close(fd);FDS.push_back(-1);}}
inline void fds_init(std::size_t n){FDS.assign(n,-1);}
inline void set_fd(std::size_t i,int fd){if(drv().rd && i<40 && i<FDS.size())FDS[i]=fd;else if(fd>=0)::close(fd);}
inline const std::vector<std::size_t>& cpu_order() {
    static const auto cpus=[] {
        std::vector<std::size_t> out;
#if defined(__linux__)
        cpu_set_t allowed;
        if(::sched_getaffinity(0,sizeof(allowed),&allowed)!=0)return out;
        std::array<bool,CPU_SETSIZE> seen{};
        for(std::size_t cpu=0;cpu<CPU_SETSIZE;++cpu){if(!CPU_ISSET(cpu,&allowed)||seen[cpu])continue;
            std::ifstream f("/sys/devices/system/cpu/cpu"+std::to_string(cpu)+"/topology/thread_siblings_list");std::string raw;std::getline(f,raw);
            std::vector<std::size_t> group;bool has=false;std::size_t pos=0;
            while(pos<raw.size()){
                auto end=raw.find(',',pos);if(end==std::string::npos)end=raw.size();auto* pf=raw.data()+pos;auto* stop=raw.data()+end;
                unsigned lo;auto [it,e]=std::from_chars(pf,stop,lo);if(e==std::errc{}){unsigned hi=lo;if(it<stop && *it=='-'){auto [ep,ec]=std::from_chars(it+1,stop,hi);if(ec!=std::errc{}||ep!=stop)hi=lo;}
                    for(unsigned j=lo;j<=std::min(hi,unsigned(CPU_SETSIZE-1));++j){has|=j==cpu;group.push_back(j);}}
                pos=end+1;
            }
            if(!has)group={cpu};for(auto i:group)if(!seen[i]&&CPU_ISSET(i,&allowed)){seen[i]=true;out.push_back(i);}
        }
#endif
        return out;
    }();return cpus;
}
inline std::size_t next_line_start(std::string_view s,std::size_t from){auto n=s.find('\n',from);return n==s.npos?s.size():n+1;}
template<class G> std::vector<Chunk> work_queue_build(const G& g) {
    const auto& d=drv();auto kind=[&](u32 f){switch(g.files[f].kind){case Kind::Ev:return d.qord==2?3:0;case Kind::Ck:return 1;case Kind::Lk:return 2;}return 0;};
    if(!d.ch){auto q=g.queue;if(d.qord)std::stable_sort(q.begin(),q.end(),[&](auto& a,auto& b){return kind(a.file)<kind(b.file);});return q;}
    std::vector<u32> order(g.files.size());std::iota(order.begin(),order.end(),0);
    if(d.qord)std::stable_sort(order.begin(),order.end(),[&](auto a,auto b){return kind(a)<kind(b);});
    std::vector<Chunk> q;std::size_t remaining=0;for(const auto& f:g.files)remaining+=f.len;
    for(auto fi:order){const auto& f=g.files[fi];for(std::size_t a=1;a<f.len;){const auto low=std::size_t{256}<<10;
        const auto bytes=std::clamp(remaining/(4*g.nt),low,std::max(low,d.ch));const auto b=std::min(a+bytes,f.len);
        q.push_back({fi,a,b});remaining-=std::min(remaining,b-a);a=b;}remaining-=std::min(remaining,std::size_t{1});}
    return q;
}
template<class G> const std::pair<std::vector<Chunk>,std::size_t>& work_queue(const G& g){
    static const auto result=[&]{auto q=work_queue_build(g);std::size_t after=0;for(std::size_t i=0;i<q.size();++i)if(g.files[q[i].file].kind!=Kind::Ev)after=i+1;return std::pair{std::move(q),after};}();return result;
}
inline std::size_t read_at(int fd,u8* dst,std::size_t bytes,std::size_t pos,std::size_t flen){
    std::size_t got=0;while(got<bytes){auto r=::pread(fd,dst+got,bytes-got,off_t(pos+got));if(r>0){got+=std::size_t(r);continue;}if(r<0 && errno==EINTR)continue;if(r<0||pos+got<flen)sys::fallback("pread");break;}return got;
}
template<class G,class T> inline void parse_buf(G& g,T& t,EvBatch& eb,CkBatch& cb,Out& out,const Tabs& tb,std::size_t tid,const FileInfo& f,std::string_view buf,std::size_t s){
    Ctx<G,T> c{g,t,eb,cb,out,tb,u64(tid),u64(f.off+s),u64(f.off+f.len)};
    switch(f.kind){case Kind::Ev:k_free::Free::events(buf,c);c.ev_flush();break;case Kind::Ck:k_free::Free::clicks(buf,c);c.ck_flush();break;case Kind::Lk:links_chunk(c,buf);break;}
}
template<class G,class T> void chunk_rd(G& g,T& t,EvBatch& eb,CkBatch& cb,Out& out,const Tabs& tb,std::size_t tid,const FileInfo& f,const Chunk& c,int fd,u8* rb,std::size_t rd){
    const auto flen=f.len,lim=std::min(c.b,flen);auto pos=c.a-1,n=read_at(fd,rb,rd,pos,flen);std::size_t o;
    auto bytes=[&]{return std::string_view(reinterpret_cast<const char*>(rb),n);};
    for(;;){o=bytes().find('\n');if(o!=std::string_view::npos){++o;break;}if(pos+n>=flen)return;pos+=n;n=read_at(fd,rb,rd,pos,flen);}
    for(;;){auto st=pos+o;if(st>=lim)return;auto s=bytes();const bool eof=pos+n>=flen;auto li=lim-pos;
        std::size_t cut=std::string_view::npos;bool done=true;if(li<=n){auto e=s.find('\n',li-1);if(e!=s.npos)cut=e+1;}
        if(cut==s.npos){if(eof)cut=n;else{auto j=s.substr(o).rfind('\n');if(j!=s.npos){cut=o+j+1;done=false;}else{
            std::string_view data(reinterpret_cast<const char*>(g.region+f.off),flen);auto e=c.b>=flen ? flen : next_line_start(data,c.b-1);parse_buf(g,t,eb,cb,out,tb,tid,f,data.substr(st,e-st),st);return;}}}
        parse_buf(g,t,eb,cb,out,tb,tid,f,s.substr(o,cut-o),st);if(done)return;pos+=cut;o=0;n=read_at(fd,rb,rd,pos,flen);
    }
}
template<class G> void stage1(G& g,std::size_t tid){
    const auto& d=drv();
#if defined(__linux__)
    cpu_set_t saved;bool pinned=false;
    if(d.pin && !(tid>=1&&tid<16&&sys::PINNED[tid].load(std::memory_order_acquire))){const auto& cpus=cpu_order();if(!cpus.empty()&&::sched_getaffinity(0,sizeof(saved),&saved)==0)pinned=sys::pin_to(cpus[tid%cpus.size()]);}
#endif
    auto& t=g.t(tid);
    if(PARTS_READY.load(std::memory_order_relaxed)){t.ev.set_arena(g.ev_arena);t.us.set_arena(g.us_arena);}
    else{t.ev=Part(g.ev_arena,g.nb);t.us=Part(g.us_arena,g.nub);}
    if(!CORE_READY.load(std::memory_order_relaxed))t.core=core::Core(g.nc,g.nd,g.row_cap,g.ws_mod,g.day0);
    auto eb=std::make_unique<EvBatch>();auto cb=std::make_unique<CkBatch>();auto out=std::make_unique<Out>();const auto& tb=tabs(g);
    const bool use_rd=d.rd && FDS.size()==g.files.size();auto* rb=use_rd?sys::anon(d.rd+8192)+4096:nullptr;
    const auto& [q,nckl]=work_queue(g);bool us_open=true;links::Scratch sc;
    for(;;){if(d.st2q&&!us_open)while(links::job(g,t,sc)){}
        const auto i=g.qnext.fetch_add(1,std::memory_order_relaxed);
        if(us_open&&i>=nckl){us_open=false;t.us.finish();if(g.ccap)ckix::flush(out->pd,g.csr);links::us_finished(g,out->pd.end,d.st2q);if(d.st2q)while(links::job(g,t,sc)){};}
        if(i>=q.size())break;const auto& c=q[i];const auto& f=g.files[c.file];
        if(use_rd&&FDS[c.file]>=0){chunk_rd(g,t,*eb,*cb,*out,tb,tid,f,c,FDS[c.file],rb,d.rd);continue;}
        std::string_view data(reinterpret_cast<const char*>(g.region+f.off),f.len);
        const auto s=c.a==f.hdr?c.a:next_line_start(data,c.a-1),e=c.b>=f.len?f.len:next_line_start(data,c.b-1);if(s>=e)continue;
        parse_buf(g,t,*eb,*cb,*out,tb,tid,f,data.substr(s,e-s),s);
        if(RELEASE)sys::release(g.region+f.off+s,e-s);
    }
    t.ev.finish();if(d.st2q)for(;;){if(links::job(g,t,sc))continue;if(links::done(g))break;sys::spin_loop();}
    if(rb)::madvise(rb-4096,d.rd+8192,MADV_DONTNEED);
#if defined(__linux__)
    if(pinned && (d.pin==1||tid==0))::sched_setaffinity(0,sizeof(saved),&saved);
#endif
}
}
