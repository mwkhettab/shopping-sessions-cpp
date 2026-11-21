#include "../../cpp/pipeline/keyrec.hpp"
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <iterator>
#include <string>

using namespace pipeline;
using namespace pipeline::keyrec;
static std::string read(const char *file) {
    std::ifstream f(file,std::ios::binary);
    if (!f) std::abort();
    return {std::istreambuf_iterator<char>(f),{}};
}
static void word(u64 w) {
    std::array<u8,8> b;
    for (unsigned i=0;i<8;++i) b[i]=u8(w>>(8*i));
    if (std::fwrite(b.data(),1,8,stdout)!=8) std::abort();
}
int main(int argc,char **argv) {
    if (argc!=3) return 2;
    u64 state=67842;
    for (unsigned i=0;i<100000;++i) {
        state=state*6364136223846793005ULL+1442695040888963407ULL;
        auto [v,ok]=base36_word(state,i%8+1);
        word(v);word(ok);word(scramble(state,1923888191));word(unscramble(state,900712138));
        std::vector<u8> spelling; base36_encode(state&0xffffffffffULL,spelling);
        word(base36_decode_swar({reinterpret_cast<const char*>(spelling.data()),spelling.size()}).value_or(UINT64_MAX));
    }
    Sample s;
    const auto events=read(argv[1]),clicks=read(argv[2]);
    s.add_event_lines(events,17000); s.add_click_lines(clicks,12000);
    word(s.events.size());word(s.clicks.size());
    for (auto r:s.events) {word(r.id_x);word(r.user_x.value_or(UINT64_MAX));word(u64(r.ts.value_or(INT64_MIN)));}
    for (auto r:s.clicks) {word(r.id_x);word(r.user_x.value_or(UINT64_MAX));}
    Stats st;auto keys=recover(s,{},st);
    word(keys?keys->user:UINT64_MAX);word(keys?keys->event:UINT64_MAX);word(keys?keys->click:UINT64_MAX);
    word(st.user_candidates_tried);word(st.event_steps);word(st.click_steps);
}
