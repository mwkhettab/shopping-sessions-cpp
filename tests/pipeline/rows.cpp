#include "../../cpp/pipeline/k_ref.hpp"
#include <cstdio>
#include <fstream>
#include <iterator>
#include <string>

using namespace pipeline;
void word(u64 n) {
    u8 b[8];
    for (unsigned i=0; i<8; ++i) b[i]=u8(n >> (8*i));
    if (std::fwrite(b,1,8,stdout) != 8) std::abort();
}
struct Sink {
    EvBatch eb;
    CkBatch cb;
    EvBatch *ev() { return &eb; }
    CkBatch *ck() { return &cb; }
    void ev_flush() {
        for (std::size_t i=0; i<eb.n; ++i) {
            auto r=eb.row(i);
            word(eb.pp[i]); word(r.eid); word(r.uid); word(u64(r.ts));
            word(u64(r.ingest)); word(r.kind); word(r.product); word(r.price);
        }
        eb.n=0;
    }
    void ck_flush() {
        for (std::size_t i=0; i<cb.n; ++i) {
            auto r=cb.row(i);
            word(r.cid); word(r.uid); word(r.campaign); word(u64(r.ts));
        }
        cb.n=0;
    }
};
int main(int argc,char **argv) {
    if (argc!=3) return 2;
    std::ifstream f(argv[2],std::ios::binary);
    if (!f) return 2;
    std::string s{std::istreambuf_iterator<char>(f),{}};
    Sink sink;
    if (std::string_view(argv[1])=="events") {
        k_ref::events_with(s,[&](const EvRow &r,auto a,auto b){
            word(a);word(b);word(r.eid);word(r.uid);word(u64(r.ts));word(u64(r.ingest));
            word(r.kind);word(r.product);word(r.price);
        });
        k_ref::Ref::events(s,sink); sink.ev_flush();
    } else {
        k_ref::clicks_with(s,[&](const ClRow &r,auto a,auto b){
            word(a);word(b);word(r.cid);word(r.uid);word(r.campaign);word(u64(r.ts));
        });
        k_ref::Ref::clicks(s,sink); sink.ck_flush();
    }
}
