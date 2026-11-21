#include "setup.hpp"
#include <cassert>
#include <cstdio>
using namespace pipeline;
static u64 digest(std::span<const u8> bytes){u64 h=0xcbf29ce484222325ull;for(u8 c:bytes)h=(h^c)*0x100000001b3ull;return h;}
int main(int argc,char** argv){
    if(argc<3)return 2;
    constexpr std::string_view rows="p4,\"/\"\np2,clothes\n\"p99\",unknown\np6,books\np1,clothes";
    const auto serial=setup::products_serial(rows);
    for(std::size_t k:{1u,2u,13u,48u}){std::vector<std::optional<setup::ProdPart>> parts;
        for(std::size_t i=0;i<k;++i)parts.push_back(setup::products_part(setup::piece(rows,i,k)));
        auto p=setup::products_merge(parts);assert(p.cats==serial.cats && std::equal(serial.prod.begin(),serial.prod.end(),p.prod.begin()));}
    for(auto bad:{"p0007,books","p2,a,b","p10,\"misplaced",",books","p2,"})assert(!setup::products_part(bad));
    sys::alloc_enable();const bool parallel=std::string_view(argv[2])=="parallel";if(parallel)sys::pool_start(3);
    auto g=setup::setup(argv[1],3,false);assert(g->workers.size()==3&&g->qnext==0&&g->nb*BSIZE==g->ecap&&g->nub*UBSIZE==g->ucap);
    assert(bool(parse::CORE_READY)==parallel && bool(parse::PARTS_READY)==parallel);
    auto products=setup::products_serial(setup::body(setup::read(setup::Path(argv[1])/"products.csv")));
    assert(products.cats==g->cats && std::equal(products.prod.begin(),products.prod.end(),g->prod.begin()));
    const auto all=setup::all_files(argv[1]);assert(all.size()==g->files.size());
    for(std::size_t i=0;i<all.size();++i){const auto f=g->files[i];assert(f.off%(2u<<20)==0&&f.len==all[i].len&&f.kind==all[i].kind);}
    std::printf("%llx %llx %llx %llu %llu %llu %zu %zu %zu %zu %llu %llu\n",(unsigned long long)g->ukey,(unsigned long long)g->ekey,(unsigned long long)g->ckey,(unsigned long long)g->ccap,(unsigned long long)g->ecap,(unsigned long long)g->ucap,g->nc,g->files.size(),g->queue.size(),g->row_cap,(unsigned long long)digest(std::span(g->prod).first(PBOUND+1)),(unsigned long long)digest(std::span(g->camp).first(CBOUND)));
}
