#pragma once
#include "session_simde.hpp"
using __mmask16=simde__mmask16;
inline uint8_t routing_mm512_cmplt_epu64_mask(__m512i a,__m512i b){return uint8_t(~_mm512_cmple_epu64_mask(b,a));}
inline uint8_t routing_mm512_cmpneq_epi64_mask(__m512i a,__m512i b){return uint8_t(~_mm512_cmpeq_epi64_mask(a,b));}
inline uint8_t routing_mm512_mask_cmplt_epu64_mask(unsigned m,__m512i a,__m512i b){return uint8_t(m&routing_mm512_cmplt_epu64_mask(a,b));}
inline uint8_t routing_mm256_cmplt_epu64_mask(__m256i a,__m256i b){return uint8_t(15&~_mm256_cmple_epu64_mask(b,a));}
inline uint8_t routing_mm256_mask_cmplt_epu64_mask(unsigned m,__m256i a,__m256i b){return uint8_t(m&routing_mm256_cmplt_epu64_mask(a,b));}
inline __m512i routing_mm512_mask_i64gather_epi64(__m512i dst,unsigned m,__m512i idx,const void* data,int scale){auto v=simde__m512i_to_private(dst),i=simde__m512i_to_private(idx);for(unsigned j=0;j<8;++j)if(m&(1u<<j)){uint64_t x;memcpy(&x,static_cast<const char*>(data)+i.i64[j]*scale,8);v.u64[j]=x;}return simde__m512i_from_private(v);}
inline __m256i routing_mm256_mmask_i64gather_epi64(__m256i dst,unsigned m,__m256i idx,const void* data,int scale){auto v=simde__m256i_to_private(dst),i=simde__m256i_to_private(idx);for(unsigned j=0;j<4;++j)if(m&(1u<<j)){uint64_t x;memcpy(&x,static_cast<const char*>(data)+i.i64[j]*scale,8);v.u64[j]=x;}return simde__m256i_from_private(v);}
inline void routing_mm512_mask_storeu_epi64(void* dst,unsigned m,__m512i v){const auto a=simde__m512i_to_private(v);for(unsigned j=0;j<8;++j)if(m&(1u<<j)){uint64_t x=a.u64[j];memcpy(static_cast<char*>(dst)+8*j,&x,8);}}
inline uint8_t routing_mm512_mask_cmpneq_epi64_mask(unsigned m,__m512i a,__m512i b){return uint8_t(m&routing_mm512_cmpneq_epi64_mask(a,b));}
