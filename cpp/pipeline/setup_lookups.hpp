#pragma once
#include "model.hpp"
#include "parse.hpp"
#include <filesystem>
#include <fcntl.h>
#include <sys/stat.h>

namespace pipeline::setup {
using Path=std::filesystem::path;
using Bytes=std::string_view;
using Names=std::vector<std::vector<u8>>;
inline Bytes text(const std::vector<u8>& v){return {reinterpret_cast<const char*>(v.data()),v.size()};}
inline std::vector<u8> owned(Bytes b){return {b.begin(),b.end()};}
inline std::string read(const Path& path) {
    std::ifstream f(path,std::ios::binary|std::ios::ate);if(!f)sys::fallback("cannot read a lookup file");auto n=f.tellg();if(n<0)sys::fallback("cannot read a lookup file");
    std::string out(std::size_t(n),'\0');f.seekg(0);if(n && !f.read(out.data(),n))sys::fallback("cannot read a lookup file");return out;
}
inline std::optional<std::pair<Bytes,Bytes>> split2(Bytes line) {
    const auto sep=line.find(',');if(sep==line.npos || line.find(',',sep+1)!=line.npos)return {};
    auto a=k_ref::field_value(line,0,sep),b=k_ref::field_value(line,sep+1,line.size());if(!a || !b)return {};
    return std::pair{line.substr(a->first,a->second-a->first),line.substr(b->first,b->second-b->first)};
}
inline Bytes body(Bytes b){const auto h=b.find('\n');return h==b.npos ? b.substr(b.size()) : b.substr(h+1);}
inline std::optional<i64> manifest_ts(Bytes txt,Bytes key) {
    auto k=txt.find(key);if(k==txt.npos)return {};auto c=txt.find(':',k+key.size());if(c==txt.npos)return {};
    auto q1=txt.find('"',c+1);if(q1==txt.npos)return {};auto q2=txt.find('"',q1+1);if(q2==txt.npos)return {};
    auto v=k_ref::parse_f1(txt.substr(q1+1,q2-q1-1));return v==TS_BAD ? std::nullopt : std::optional(v);
}
inline std::pair<i64,i64> window(const Path& data){
    auto m=read(data/"manifest.json");auto ws=manifest_ts(m,"\"window_start\""),we=manifest_ts(m,"\"window_end\"");
    if(!ws||!we)sys::fallback("manifest");if(*we<=*ws||*we-*ws>24*DAY_MS||*ws<8*DAY_MS)sys::fallback("window");return {*ws,*we};
}
inline std::size_t find3_plain(Bytes s,std::size_t i){while(i<s.size() && s[i]!='\n' && s[i]!=',' && s[i]!='"')++i;return i;}
#if defined(__x86_64__)
__attribute__((target("avx2"))) inline std::size_t find3_x86(Bytes s,std::size_t i){
    const auto lf=_mm256_set1_epi8('\n'),cm=_mm256_set1_epi8(','),qt=_mm256_set1_epi8('"');
    while(i+32<=s.size()){auto v=_mm256_loadu_si256(reinterpret_cast<const __m256i*>(s.data()+i));auto m=u32(_mm256_movemask_epi8(_mm256_or_si256(_mm256_or_si256(_mm256_cmpeq_epi8(v,lf),_mm256_cmpeq_epi8(v,cm)),_mm256_cmpeq_epi8(v,qt))));if(m)return i+std::countr_zero(m);i+=32;}
    return find3_plain(s,i);
}
#endif
inline std::size_t find3(Bytes b,std::size_t i){
#if defined(__x86_64__)
    if(__builtin_cpu_supports("avx2"))return find3_x86(b,i);
#endif
    return find3_plain(b,i);
}
template<class Add> bool scan_products(Bytes b,Add&& add) {
    for(std::size_t s=0;s<b.size();){const auto e=find3(b,s);
        if(e<b.size() && b[e]==',' && b[s]=='p' && e-s>=2 && e-s<=9 && !(b[s+1]=='0' && e-s>2)){
            const auto ds=b.substr(s+1,e-s-1);if(std::all_of(ds.begin(),ds.end(),k_ref::digit)){
                const auto e2=find3(b,e+1);if((e2==b.size() || b[e2]=='\n') && e2>e+1){u32 n=0;for(auto c:ds)n=10*n+u32(c-'0');if(!add(n,b.substr(e+1,e2-e-1)))return false;s=e2+1;continue;}
            }
        }
        auto le=b.find('\n',s);if(le==b.npos)le=b.size();auto f=split2(b.substr(s,le-s));
        if(!f||f->second.empty()||!add(k_ref::parse_product(f->first),f->second))return false;s=le+1;
    }return true;
}
struct Interner {
    Names names;
    std::array<u8,64> tab;
    Interner(){tab.fill(255);}
    std::optional<u8> id(Bytes c){u64 h=0xcbf29ce484222325ULL;for(u8 x:c)h=(h^x)*0x100000001b3ULL;
        for(auto s=std::size_t(h>>58);;s=(s+1)&63){auto& k=tab[s];if(k==255){if(names.size()>=31)return {};k=u8(names.size());names.push_back(owned(c));return k;}if(text(names[k])==c)return k;}}
};
struct ProdPart {std::vector<std::pair<u32,u8>> pairs;Names names;};
struct Products {std::vector<u8> prod;Names cats;};
struct Campaigns {std::vector<u8> camp;Names channels;std::size_t nc{};};
inline std::optional<ProdPart> products_part(Bytes b){
    ProdPart p;p.pairs.reserve(b.size()/16+16);Interner inter;
    if(!scan_products(b,[&](u32 n,Bytes c){if(n>=PBOUND)return false;auto ci=inter.id(c);if(!ci)return false;p.pairs.emplace_back(n,*ci);return true;}))return {};
    p.names=std::move(inter.names);return p;
}
inline std::pair<Names,std::vector<u8>> category_order(const Names& names){
    auto cats=names;cats.push_back(owned("unknown"));std::sort(cats.begin(),cats.end());cats.erase(std::unique(cats.begin(),cats.end()),cats.end());if(cats.size()>31)sys::fallback("too many categories");
    std::vector<u8> remap;remap.reserve(names.size());for(const auto& name:names)remap.push_back(u8(std::find(cats.begin(),cats.end(),name)-cats.begin()));return {std::move(cats),std::move(remap)};
}
inline Products products_merge(const std::vector<std::optional<ProdPart>>& parts){
    Names names;std::vector<std::vector<u8>> maps(parts.size());
    for(std::size_t i=0;i<parts.size();++i){if(!parts[i])sys::fallback("products.csv is not canonical, or has too many categories");
        for(const auto& c:parts[i]->names){auto it=std::find(names.begin(),names.end(),c);auto n=std::size_t(it-names.begin());if(it==names.end()){if(n>=31)sys::fallback("products.csv is not canonical, or has too many categories");names.push_back(c);}maps[i].push_back(u8(n));}}
    auto [cats,remap]=category_order(names);const auto unknown=u8(std::find(cats.begin(),cats.end(),owned("unknown"))-cats.begin());std::vector<u8> p(PBOUND+65,255);
    for(std::size_t i=0;i<parts.size();++i)for(auto [n,k]:parts[i]->pairs){if(p[n]!=255)sys::fallback("products.csv is not canonical, or has too many categories");p[n]=remap[maps[i][k]];}
    std::replace(p.begin(),p.begin()+PBOUND+1,u8(255),unknown);std::fill(p.begin()+PBOUND+1,p.end(),0);return {std::move(p),std::move(cats)};
}
inline Products products_serial(Bytes b) {
    std::vector<u8> p(PBOUND+1,255);Interner inter;
    if(!scan_products(b,[&](u32 n,Bytes c){if(n>=PBOUND||p[n]!=255)return false;auto k=inter.id(c);if(!k)return false;p[n]=*k;return true;}))sys::fallback("products.csv is not canonical, or has too many categories");
    auto [cats,remap]=category_order(inter.names);const auto unknown=u8(std::find(cats.begin(),cats.end(),owned("unknown"))-cats.begin());for(auto& k:p)k=k==255?unknown:remap[k];return {std::move(p),std::move(cats)};
}
inline Campaigns campaigns(const Path& data,bool padding=true){
    Campaigns out;out.camp.assign(CBOUND+(padding?64:0),255);auto file=read(data/"campaigns.csv");auto b=body(file);
    k_ref::for_each_line(b,[&](auto i,auto j){auto f=split2(b.substr(i,j-i));if(!f || f->second.empty())sys::fallback("campaigns.csv is not canonical");
        const auto n=k_ref::parse_campaign(f->first);if(n>=CBOUND||out.camp[n]!=255)sys::fallback("campaigns.csv is not canonical");
        auto& ch=out.channels;auto it=std::find_if(ch.begin(),ch.end(),[&](auto& name){return text(name)==f->second;});auto k=std::size_t(it-ch.begin());if(k>=250)sys::fallback("campaigns.csv is not canonical");
        if(it==ch.end())ch.push_back(owned(f->second));out.camp[n]=u8(k);out.nc=std::max(out.nc,std::size_t(n)+1);
    });return out;
}
inline Bytes map_file(const Path& p){
    auto fd=::open(p.c_str(),O_RDONLY);if(fd<0)sys::fallback("cannot read a lookup file");struct stat st{};if(::fstat(fd,&st)<0||st.st_size<0)sys::fallback("cannot read a lookup file");
    if(st.st_size==0){::close(fd);return {};}
    auto q=::mmap(nullptr,std::size_t(st.st_size),PROT_READ,MAP_PRIVATE,fd,0);::close(fd);if(q==MAP_FAILED)sys::fallback("cannot read a lookup file");return {static_cast<const char*>(q),std::size_t(st.st_size)};
}
inline Bytes piece(Bytes b,std::size_t j,std::size_t k){auto at=[&](std::size_t i){return (i>0&&i<b.size())?parse::next_line_start(b,i-1):i;};auto a=at(b.size()*j/k),e=at(b.size()*(j+1)/k);return b.substr(a,e-a);}
}
