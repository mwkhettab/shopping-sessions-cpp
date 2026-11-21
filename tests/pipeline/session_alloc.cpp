#include "../../cpp/pipeline/sys.hpp"
#include <cassert>
#include <dlfcn.h>
#include <iostream>
#include <limits>
#include <string>
using namespace pipeline;
using namespace pipeline::sys;
struct Block {u8 *p{};std::size_t n{},align{};u8 pattern{};};
void check(const Block& b) {
    assert(b.p && (reinterpret_cast<std::uintptr_t>(b.p)&(b.align-1))==0);
    for(std::size_t i=0;i<b.n;i+=43) assert(b.p[i]==b.pattern);
    if(b.n) assert(b.p[b.n-1]==b.pattern);
}
void drop(const Block& b,std::size_t kind) {
    if (kind&1) ::operator delete[](b.p,std::align_val_t(b.align));
    else ::operator delete[](b.p,b.n,std::align_val_t(b.align));
}
int main(int argc,char** argv) {
    std::vector<u64> pre(1024,47);assert(!a_mine(pre.data()));
    // The shared library returns a normal C++ object using the same global operators.
    std::string s(100,'q');std::string initial=s;
    alloc_enable();alloc_enable();assert(A_BASE.load());
    std::vector<u8> managed(80000,169);assert(a_mine(managed.data()));
    std::vector<u8> foreign(200000,199);assert(a_mine(foreign.data()));
    struct alignas(256) Over {u64 v[32];};
    Over *over=new Over[45]{};assert(a_mine(over) && !(reinterpret_cast<std::uintptr_t>(over)&255));delete[] over;
    constexpr std::size_t N=360,NT=4;
    std::array<std::array<Block,N>,NT> batch{};
    std::array<std::thread,NT> writers;
    for(std::size_t t=0;t<NT;++t) writers[t]=std::thread([&,t]{
        for(std::size_t i=0;i<N;++i) {
            const auto align=std::size_t{1}<<(4+((i+t*3)%11));
            const auto sz=(i<6)?i:(std::size_t{1}<<((i*11+t)%16))+(i%3)-1;
            auto* p=static_cast<u8*>(::operator new[](sz,std::align_val_t(align),std::nothrow));
            assert(p && (a_mine(p)==(align<=4096)));
            batch[t][i]={p,sz,align,u8(i+t+7)};
            std::memset(p,batch[t][i].pattern,sz);
        }
    });
    for(auto& w:writers) w.join();
    for(std::size_t t=0;t<NT;++t) writers[t]=std::thread([&,t]{
        for(std::size_t i=0;i<N;++i) {
            auto& b=batch[(t+1)%NT][i];check(b);drop(b,i);
            if (b.align<=4096) {
                auto* other=::operator new[](b.n,std::align_val_t(b.align));
                assert(other==b.p);::operator delete[](other,std::align_val_t(b.align));
            }
        }
    });
    for(auto& w:writers) w.join();
    if (argc==2) {
        void *lib=::dlopen(argv[1],RTLD_NOW|RTLD_LOCAL);assert(lib);
        auto make=reinterpret_cast<void*(*)(std::size_t,std::size_t)>(::dlsym(lib,"library_allocate"));
        auto drop_in_lib=reinterpret_cast<void(*)(void*,std::size_t)>(::dlsym(lib,"library_release"));
        assert(make && drop_in_lib);
        for(auto al:{16u,64u,4096u,16384u}) {
            auto *from_lib=make(599,al);std::memset(from_lib,41,599);
            ::operator delete[](from_lib,std::align_val_t(al));
            auto *from_app=::operator new[](531,std::align_val_t(al));
            std::memset(from_app,42,531);drop_in_lib(from_app,al);
        }
        assert(!::dlclose(lib));
    }
    auto *huge=::operator new(HP+300,std::nothrow);
    assert(huge && a_mine(huge));std::memset(huge,0x5a,HP+300);
    auto* sys=::operator new(367,std::align_val_t(16384));
    assert(!a_mine(sys));::operator delete(sys,367,std::align_val_t(16384));
    const auto max=std::numeric_limits<std::size_t>::max();
    assert(!::operator new(max,std::nothrow));
    bool threw=false;try {auto* impossible=::operator new(max);::operator delete(impossible);} catch(const std::bad_alloc&){threw=true;}
    assert(threw);
    auto* unprotected=anon(8*HP);
    for(unsigned i=0;i<8;++i) unprotected[i*HP]=42;
    for(std::size_t t=0;t<NT;++t) writers[t]=std::thread([t]{release_heap_part(t,NT);});
    for(unsigned i=0;i<192;++i) {
        const auto n=(112+i%24)*1024;
        auto* p=static_cast<u8*>(::operator new(n));std::memset(p,int(i+1),n);
        for(unsigned j=0;j<n;j+=4096) assert(p[j]==u8(i+1));
        ::operator delete(p,n);
    }
    for(auto& w:writers) w.join();
    assert(std::all_of(managed.begin(),managed.end(),[](auto x){return x==169;}));
    assert(std::all_of(foreign.begin(),foreign.end(),[](auto x){return x==199;}));
    assert(std::all_of(pre.begin(),pre.end(),[](auto x){return x==47;}));assert(s==initial);
    auto* h=static_cast<u8*>(huge);for(std::size_t i=0;i<HP+300;i+=97)assert(h[i]==0x5a);
    ::operator delete(huge);
    // A full bump region must fall back through malloc without reentering new.
    const auto end=A_END.load();A_END.store(A_NEXT.load());
    auto* outside=::operator new(7*HP+43);assert(!a_mine(outside));::operator delete(outside);
    A_END.store(end);
    std::cout<<"1,440 cross-thread frees, alignment, release and system fallback matched\n";
}
