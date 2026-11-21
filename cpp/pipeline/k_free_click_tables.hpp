// Included after the baseline timestamp and ID lookup tables.
#pragma once

struct alignas(128) ClickIdPair {
    A<u8,32> ctrl{}, subtract{}, leading_zero{}, padding{};
};

inline constexpr auto CLICK_ID_PAIRS K_FREE_ASM_NAME("shopping_click_id_pairs") = [] {
    constexpr auto clicks = id_shapes_raw('k',0,8,false);
    constexpr auto users = id_shapes_raw('u',0,8,true);
    ClickIdPair invalid{};
    invalid.ctrl.fill(0x80);
    invalid.subtract[8] = invalid.subtract[24] = 1;
    A<ClickIdPair,34*32> pairs{};
    pairs.fill(invalid);
    // The scan returns position 32 when no delimiter is present.
    for (unsigned first=0; first<32; ++first) {
        for (unsigned second=first+1; second<32; ++second) {
            auto &out = pairs[first*32+second];
            const auto &click = clicks[first];
            const unsigned width = second-first-1;
            const auto &user = users[width];
            for (unsigned i=0; i<16; ++i) {
                out.ctrl[i] = click.ctrl[i];
                out.subtract[i] = click.subv[i];
                out.leading_zero[i] = click.lz[i];
            }
            if (user.orv[1] || width>15) continue;
            for (unsigned i=0; i<16; ++i) {
                const auto s = user.ctrl[i];
                out.ctrl[16+i] = s==0x80 ? s : u8(s+width-15);
                out.subtract[16+i] = user.subv[i];
                out.leading_zero[16+i] = user.lz[i];
            }
        }
    }
    return pairs;
}();

struct alignas(64) ClickCampaign {
    A<u8,16> ctrl{}, subtract{}, limit{}, padding{};
};

inline constexpr auto CLICK_CAMPAIGNS K_FREE_ASM_NAME("shopping_click_campaigns") = [] {
    ClickCampaign invalid{};
    invalid.ctrl.fill(0x80);
    invalid.subtract[8] = 1;
    A<ClickCampaign,320> rows{};
    rows.fill(invalid);
    for (unsigned quoted=0; quoted<=1; ++quoted) {
        for (unsigned digits=1; digits<=4; ++digits) {
            const unsigned width=2+digits+2*quoted, first=15-width, start=15-quoted-digits;
            const unsigned lead=quoted ? '"' : 'c';
            auto &s=rows[lead+64-2*(width+1)];
            s.ctrl.fill(0x80);
            s.subtract.fill(0);
            for (unsigned i=0; i<4; ++i) s.limit[i]=9;
            for (unsigned i=0; i<digits; ++i) {
                s.ctrl[4-digits+i]=u8(start+i);
                s.subtract[4-digits+i]='0';
            }
            s.ctrl[8]=u8(first+quoted); s.subtract[8]='c';
            s.ctrl[9]=u8(first+quoted+1); s.subtract[9]='_';
            s.ctrl[10]=15; s.subtract[10]=',';
            if (quoted) {
                s.ctrl[11]=14; s.subtract[11]='"';
                s.ctrl[12]=u8(first); s.subtract[12]='"';
            }
            s.ctrl[13]=u8(first-1); s.subtract[13]=',';
            if (digits>1) {
                s.ctrl[14]=u8(start);
                s.subtract[14]='1';
                s.limit[14]=8;
            }
        }
    }
    return rows;
}();

inline constexpr auto CLICK_CLOCKS K_FREE_ASM_NAME("shopping_click_clocks") = [] {
    auto rows=CTS;
    for (unsigned quoted=0; quoted<=1; ++quoted) {
        auto &r=rows[26+2*quoted];
        r.base[15]='-';
        r.w1[14]=r.w1[15]=1;
        r.limv[3]=255;
        r.ksel.fill(0);
        r.ksel[15]=0x80;
        r.kmf={0,-60000};
    }
    return rows;
}();

struct alignas(64) ClickConstants {
    A<u8,32> comma{}, newline{}, letter_bias{}, letter_limit{}, digit_limit{}, digit_bias{};
    A<u16,16> radix_pair{};
    A<u32,8> radix_four{};
    A<u64,4> radix_eight{};
    A<u8,16> decimal_pair{};
    A<u32,4> decimal_four{};
    A<u64,2> offset_sign{};
};

inline constexpr auto CLICK_CONSTANTS K_FREE_ASM_NAME("shopping_click_constants") = [] {
    ClickConstants c{};
    c.comma.fill(','); c.newline.fill('\n'); c.digit_bias.fill(217);
    for (unsigned lane: {0u,16u}) {
        for (unsigned i=0; i<8; ++i) {
            c.letter_bias[lane+i]=207;
            c.letter_limit[lane+i]=25;
            c.digit_limit[lane+i]=9;
        }
    }
    c.radix_pair.fill(0x0124);
    c.radix_four.fill(0x00010510);
    c.radix_eight.fill(1679616);
    c.decimal_pair={10,1,10,1};
    c.decimal_four={0x00010064};
    c.offset_sign={0,u64{0x80}<<56};
    return c;
}();

static_assert(sizeof(ClickIdPair)==128 && sizeof(ClickCampaign)==64);
static_assert(offsetof(ClickConstants,radix_pair)==192 && offsetof(ClickConstants,offset_sign)==320);
