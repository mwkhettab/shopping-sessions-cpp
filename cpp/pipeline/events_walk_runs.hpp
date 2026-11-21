// Included within pipeline::events and its feature-dispatched ISA region.
template<class G> [[gnu::noinline]] std::size_t walk_runs(Fast& fx,const G& g,Walk& wk,std::size_t first,
                                                           std::size_t nrun,u32 pidx0,u32 org) {
    if(first>=nrun) return first;
    const auto begin=fx.sidx[fx.run_s0[first]]&M31;
    while(fx.mix[wk.mixp]<begin) ++wk.mixp;
    if(!first || first+1>=nrun) return first;
    const auto next_mixed=fx.mix[wk.mixp];
    std::size_t stop=first,at=begin;
    while(stop+1<nrun) {
        const auto end=fx.sidx[fx.run_s0[stop+1]]&M31;const auto qa=fx.q[at];
        const auto user=u32(qa>>36);const auto tv=g.utab[user];
        if(next_mixed<end || (tv&(COMPLEX|EXTRA))) break;
        const auto mask=u32(tv&MASK),bit=u32{1}<<((qa>>32)&15);
        fx.walk_user[stop]=user;fx.walk_class[stop]=(bit&mask)?mask:bit;
        at=end;++stop;
    }
    if(stop==first) return stop;
    wk.c_runs+=stop-first;wk.c_simple+=at-begin;wk.c_sess+=fx.run_s0[stop]-fx.run_s0[first];
    std::size_t listed=0;
    for(std::size_t r=first;r<stop;++r) {
        const auto u=fx.walk_user[r];
        if((u>>6)!=wk.cw) {claim_word(g,wk.cw,wk.cb);wk.cw=u>>6;wk.cb=0;}
        const u64 ub=u64{1}<<(u&63);
        if(wk.cb&ub) sys::fallback("a user has two runs");
        wk.cb|=ub;
        if constexpr(requires{g.csr_off;g.csr_cap;}) {
            const auto pos=std::size_t(u)+(u>>UB);const auto base=g.csr_off[pos];
            const auto n=g.csr_off[pos+1]-base;
            fx.click_run[listed]=u32(r);fx.click_start[listed]=base;fx.click_size[listed]=n;listed+=n!=0;
        } else {
            if(!g.csr_range(u).empty()) fx.click_run[listed++]=u32(r);
        }
    }
    auto np=wk.nm_np,na=wk.nm_na;
    for(std::size_t r=first;r<stop;++r,++na) {
        const auto u=fx.walk_user[r],klass=fx.walk_class[r];
        fx.run_pidx[r]=pidx0+u32(na);
        const auto ncl=std::size_t(std::popcount(klass));fx.nm_best[na]=UINT64_MAX;
        if(ncl-1>=8) fx.nm_best[na]=name_key_scalar(g.ukey,u,klass);
        else {
            const auto al=_mm512_cvtepu32_epi64(_mm512_castsi512_si256(_mm512_maskz_compress_epi32(klass,iota16())));
            put512(fx.nm_v+np,_mm512_or_si512(_mm512_set1_epi64(u64(u)<<9),al));
            put256(fx.nm_r+np,_mm256_set1_epi32(na));np+=ncl;
        }
    }
    wk.nm_np=np;wk.nm_na=na;
    auto nw=wk.nw,nrc=wk.nrc;
    for(std::size_t j=0;j<listed;++j) {
        const auto r=fx.click_run[j],s0=fx.run_s0[r],nsr=fx.run_s0[r+1]-s0,klass=fx.walk_class[r];
        std::span<const std::array<u64,2>> cks;
        std::span<const u64> keys;
        if constexpr(requires{g.simple_words(fx.walk_user[r]);}) {
            if(g.simple_words(fx.walk_user[r])) keys={reinterpret_cast<const u64*>(g.csr)+fx.click_start[j],fx.click_size[j]};
        }
        if constexpr(requires{g.csr_off;g.csr_cap;}) {
            if(keys.empty()) cks={g.csr+fx.click_start[j],fx.click_size[j]};
        }
        else cks=g.csr_range(fx.walk_user[r]);
        const auto n=keys.empty()?cks.size():keys.size();
        bool light=false;
        if (n<=16 && nsr<=WL_RUN) {
            if (nrc==RC_N || nw+nsr>WL_CAP) {light_flush(fx,nw,org);nw=0;nrc=0;}
            const auto* p=cks.data();
            __m512i ka,kb;
            bool padded=false;
            if constexpr(requires{g.csr_cap;g.ncsr;}) {
                // Published CSR buffers include sixteen guard records. Small stand-alone ranges
                // use a masked boundary load when they do not share that padded storage.
                if(p) {const auto off=std::size_t(p-g.csr);padded=off<=g.csr_cap && g.csr_cap-off>=16;}
            }
            if(!keys.empty()) {
                // The partition arena has at least sixteen guard words at its end.
                ka=get512(keys.data());kb=get512(keys.data()+8);
            } else if (padded) {
                ka=_mm512_permutex2var_epi64(get512(p),even64(),get512(p+4));
                kb=_mm512_permutex2var_epi64(get512(p+8),even64(),get512(p+12));
            } else {
                auto load=[&](unsigned off) PIPELINE_EV_ATTR {
                    if (off+4<=n) return get512(p+off);
                    if (off>=n) return _mm512_setzero_si512();
                    return _mm512_maskz_loadu_epi64((1u<<(2*(n-off)))-1,p+off);
                };
                ka=_mm512_permutex2var_epi64(load(0),even64(),load(4));
                kb=_mm512_permutex2var_epi64(load(8),even64(),load(12));
            }
            const auto lo=join32(_mm512_cvtepi64_epi32(ka),_mm512_cvtepi64_epi32(kb));
            const auto ts=join32(_mm512_cvtepi64_epi32(_mm512_srli_epi64(ka,32)),_mm512_cvtepi64_epi32(_mm512_srli_epi64(kb,32)));
            const auto vm=(1u<<n)-1;
            const auto inc=_mm512_test_epi32_mask(_mm512_sllv_epi32(_mm512_set1_epi32(1),_mm512_and_si512(lo,_mm512_set1_epi32(15))),_mm512_set1_epi32(klass));
            if ((inc&vm)==vm) {
                light=true;
                const auto camp=_mm512_and_si512(lo,_mm512_set1_epi32((CBOUND-1)<<4));
                auto& rc=fx.rc[nrc];_mm512_store_si512(rc.ts,ts);_mm512_store_si512(rc.camp,camp);
                _mm512_store_si512(rc.conf,_mm512_conflict_epi32(camp));rc.vm=vm;
                for (std::size_t x=0;x<nsr;x+=16) {
                    put512(fx.wl_s+nw+x,_mm512_add_epi32(_mm512_set1_epi32(s0+x),iota16()));
                    put512(fx.wl_r+nw+x,_mm512_set1_epi32(nrc*sizeof(Rc)));
                }
                ++nrc;nw+=nsr;
            }
        }
        if (!light) {
            if(keys.empty()) run_clicks(fx,cks,klass,s0,nsr,org);
            else run_clicks_k<false>(fx,keys,klass,s0,nsr,org);
        }
    }
    wk.nw=nw;wk.nrc=nrc;return stop;
}
