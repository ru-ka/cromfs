#include "endian.hh"
#include "newhash.h"
#include <algorithm>

#include "simd.hh"

#ifdef __GNUC__
# define likely(x)       __builtin_expect(!!(x), 1)
# define unlikely(x)     __builtin_expect(!!(x), 0)
#else
# define likely(x)   (x)
# define unlikely(x) (x)
#endif

#if defined(__x86_64) || (defined(USE_MMX) && defined(__SSE2__))
/* On x86_64, we can use 64-bit registers. Which is fast.
 * On 32-bit, we can use MMX registers, if MMX is enabled
 *   However, SSE2 must ALSO be enabled, because otherwise
 *   we cannot do 64-bit sub/add efficiently.
 *
 * 128-bit is not feasible, because SSE2 does not
 * have 128-bit add/sub ops. They cannot be even synthesized
 * from 64-bit adds/subs, because there's no carry update.
 */
# define SIXTY_BIT_PLATFORM
#else
# undef SIXTY_BIT_PLATFORM
#endif

/* Based on Robert J. Jenkins Jr.'s hash code
 * References:
 *   http://www.burtleburtle.net/bob/hash/evahash.html
 *   http://www.cris.com/~Ttwang/tech/inthash.htm
 *
 * Copyright (C) 2008 Joel Yliluoma (http://iki.fi/bisqwit/)
 */

/* The mixing step */
#define mix32(a,b,c) \
do{ \
  a=(a-b-c) ^ (c>>13); \
  b=(b-c-a) ^ (a<<8);  \
  c=(c-a-b) ^ (b>>13); \
  \
  a=(a-b-c) ^ (c>>12); \
  b=(b-c-a) ^ (a<<16); \
  c=(c-a-b) ^ (b>>5);  \
  \
  a=(a-b-c) ^ (c>>3);  \
  b=(b-c-a) ^ (a<<10); \
  c=(c-a-b) ^ (b>>15); \
}while(0)

#define mix64(a,b,c) \
do{ \
  a=(a-b-c) ^ (c>>43); \
  b=(b-c-a) ^ (a<<9); \
  c=(c-a-b) ^ (b>>8); \
  \
  a=(a-b-c) ^ (c>>38); \
  b=(b-c-a) ^ (a<<23); \
  c=(c-a-b) ^ (b>>5); \
  \
  a=(a-b-c) ^ (c>>35); \
  b=(b-c-a) ^ (a<<49); \
  c=(c-a-b) ^ (b>>11); \
  \
  a=(a-b-c) ^ (c>>12); \
  b=(b-c-a) ^ (a<<18); \
  c=(c-a-b) ^ (b>>22); \
}while(0)

/*#define mix128(a,b,c) \
do{ \
  a=(a-b-c) ^ (c>> ?? ); \
  b=(b-c-a) ^ (a<< ?? ); \
  c=(c-a-b) ^ (b>> ?? ); \
  \
  a=(a-b-c) ^ (c>> ?? ); \
  b=(b-c-a) ^ (a<< ?? ); \
  c=(c-a-b) ^ (b>> ?? ); \
  \
  a=(a-b-c) ^ (c>> ?? ); \
  b=(b-c-a) ^ (a<< ?? ); \
  c=(c-a-b) ^ (b>> ?? ); \
  \
  a=(a-b-c) ^ (c>> ?? ); \
  b=(b-c-a) ^ (a<< ?? ); \
  c=(c-a-b) ^ (b>> ?? ); \
  \
  a=(a-b-c) ^ (c>> ?? ); \
  b=(b-c-a) ^ (a<< ?? ); \
  c=(c-a-b) ^ (b>> ?? ); \
}while(0)*/

newhash_t newhash_calc(const unsigned char* buf, unsigned long size)
{
    return newhash_calc_upd(0, buf, size);
}
newhash_t newhash_calc_upd(newhash_t c, const unsigned char* buf, unsigned long size)
{
#ifdef SSE2_PLATFORM
    a = b = UINT128_C(0x9e3779b97f4a7c15f39cc0605cedc834); // 2^128 / ((1+sqrt(5))/2)
    /* TODO: implement the rest */
    /* This code is current not used. */
#elif defined(SIXTY_BIT_PLATFORM)
    c64 c_cast = (uint64_t)c; { c64 c = c_cast;
    c64 a,b;
    unsigned long len = size;
    a = b = UINT64_C(0x9e3779b97f4a7c13); // 2^64 / ((1+sqrt(5))/2)
    while(len >= 24)
    {
        a += (c64)R64(buf+0);
        b += (c64)R64(buf+8);
        c += (c64)R64(buf+16);
        mix64(a,b,c);
        buf += 24; len -= 24;
    }
    /*------------------------------------- handle the last 23 bytes */
    c = c + uint64_t(size);
    if(len >16)      { c += (c64)Rn(buf+15, len-15) & ~UINT64_C(0xFF);
                       b += (c64)R64(buf+8);
                       a += (c64)R64(buf); }
    else if(len > 8) { b += (c64)Rn(buf+8,  std::min(8UL, len-8));
                       a += (c64)R64(buf); }
    else               a += (c64)Rn(buf, std::min(8UL, len));
    /* the first byte of c is reserved for the length */
    mix64(a,b,c);
    /*-------------------------------------------- report the result */
  #ifdef USE_MMX
    newhash_t result = R32(&c.value); /* Note: this returns just the lowest 32 bits of the hash */
    MMX_clear();
    return result;
  #else
    return c.value; /* Note: this returns just the lowest 32 bits of the hash */
  #endif
   }
#else
    uint_least32_t a,b;
    unsigned long len = size;
    a = b = UINT32_C(0x9e3779b9); // 2^32 / ((1+sqrt(5))/2
    while(len >= 12)
    {
        a += R32(buf+0);
        b += R32(buf+4);
        c += R32(buf+8);
        mix32(a,b,c);
        buf += 12; len -= 12;
    }
    /*------------------------------------- handle the last 11 bytes */
    c = c+size;
    if(len >8)       { c += Rn(buf+7, len-7) & ~UINT32_C(0xFF);
                       b += R32(buf+8);
                       a += R32(buf); }
    else if(len > 4) { b += Rn(buf+4,  std::min(4UL, len-4));
                       a += R32(buf); }
    else               a += Rn(buf, std::min(4UL, len));
    /* the first byte of c is reserved for the length */
    mix32(a,b,c);
    /*-------------------------------------------- report the result */
    return c;
#endif
}
