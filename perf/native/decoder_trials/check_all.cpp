#include "k_free_tables.hpp"
#include <algorithm>
#include <cstdio>
#include <cstring>
using namespace pipeline::k_free::detail;
int main() {
    for (unsigned event = 0; event < 64; ++event)
        for (unsigned user = 0; user < 64; ++user)
            if (std::memcmp(&T.jid[event][user], &T.jid[std::min(event, 12u)][std::min(user, 13u)], sizeof(JidRow))) {
                std::fprintf(stderr, "different at widths %u and %u\n",event,user);
                return 1;
            }
    std::puts("All 4096 pairs of raw widths have identical validation and decoding masks.");
}
