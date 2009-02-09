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
 */
# define SIXTY_BIT_PLATFORM
#else
# undef SIXTY_BIT_PLATFORM
#endif

#if defined(__GNUC__) && defined(__LP64__) && !defined(__ICC)
//# define HUNDREDTWENTYEIGHTBIT_PLATFORM
/*
 * 128-bit with SSE2 is not feasible, because SSE2 does not
 * have 128-bit add/sub ops. They cannot be even synthesized
 * from 64-bit adds/subs, because there's no carry update.
 * More importantly there's no 128-bit shift to left or right.
 *
 * __attribute__((mode(TI))) can be used to created a 128-bit
 * integer type on GCC, however, it does not work on ICC.
 *
 * Most importantly though, we lack the following important
 * things:
 *   - mix128 algorithm. I don't know how to create it.
 */

#ifdef __SSE2__
 #include <xmmintrin.h>
#endif

#else
# undef HUNDREDTWENTYEIGHTBIT_PLATFORM
#endif

/* Based on Robert J. Jenkins Jr.'s hash code
 * References:
 *   http://www.burtleburtle.net/bob/hash/evahash.html
 *   http://www.cris.com/~Ttwang/tech/inthash.htm
 *
 * Copyright (C) 2008 Joel Yliluoma (http://iki.fi/bisqwit/)
 */

/* The mixing step */
#define mix32(a,b,c)  \
do{ \
  a=(a-b-c) ^ (c>> 5); \
  b=(b-c-a) ^ (a<<15); \
  c=(c-a-b) ^ (b>>13); \
  \
  a=(a-b-c) ^ (c>>11); \
  b=(b-c-a) ^ (a<< 7); \
  c=(c-a-b) ^ (b>> 5); \
  \
  a=(a-b-c) ^ (c>> 3); \
  b=(b-c-a) ^ (a<<13); \
  c=(c-a-b) ^ (b>>15); \
}while(0)

#define mix64(a,b,c)  \
do{ \
  a=(a-b-c) ^ (c>>26); \
  b=(b-c-a) ^ (a<<38); \
  c=(c-a-b) ^ (b>> 8); \
  \
  a=(a-b-c) ^ (c>>12); \
  b=(b-c-a) ^ (a<<31); \
  c=(c-a-b) ^ (b>>19); \
  \
  a=(a-b-c) ^ (c>>30); \
  b=(b-c-a) ^ (a<<15); \
  c=(c-a-b) ^ (b>>13); \
  \
  a=(a-b-c) ^ (c>>10); \
  b=(b-c-a) ^ (a<<24); \
  c=(c-a-b) ^ (b>>25), \
}while(0)

#define mix128(a,b,c) \
do{ \
  a=(a-b-c) ^ (c>> 56 ); \
  b=(b-c-a) ^ (a<< 77 ); \
  c=(c-a-b) ^ (b>> 60 ); \
  \
  a=(a-b-c) ^ (c>> 75 ); \
  b=(b-c-a) ^ (a<< 30 ); \
  c=(c-a-b) ^ (b>> 33 ); \
  \
  a=(a-b-c) ^ (c>> 38 ); \
  b=(b-c-a) ^ (a<< 58 ); \
  c=(c-a-b) ^ (b>> 71 ); \
  \
  a=(a-b-c) ^ (c>> 88 ); \
  b=(b-c-a) ^ (a<< 65 ); \
  c=(c-a-b) ^ (b>> 23 ); \
  \
  a=(a-b-c) ^ (c>> 15 ); \
  b=(b-c-a) ^ (a<< 101); \
  c=(c-a-b) ^ (b>> 71 ); \
}while(0)

#ifdef HUNDREDTWENTYEIGHTBIT_PLATFORM
typedef unsigned int uint128_t __attribute__((mode(TI)));

class c128
{
public:
    uint128_t value;
public:
    c128() : value()
    {
    }
    c128(uint128_t v) : value(v) { }
    c128(uint_fast64_t a, uint_fast64_t b)
        : value(a)
    {
        value <<= 64;
        value |= b;
    }
    c128(uint_least64_t a) : value(a)
    {
    }
    c128(uint_least32_t a) : value(a)
    {
    }
    #ifdef __SSE2__
    c128(const __m128& b) : value(*(const uint128_t*)&b)
    {
    }
    #endif

    c128& operator += (const c128& b) { value += b.value; return *this; }
    c128& operator -= (const c128& b) { value -= b.value; return *this; }
    c128& operator ^= (const c128& b)
    {
    #ifdef __SSE2__
        *(__m128*)&value = _mm_xor_ps( *(const __m128*)&value, *(const __m128*)&b.value);
    #else
        value ^= b.value;
    #endif
        return *this;
    }
    c128& operator &= (const c128& b)
    {
    #ifdef __SSE2__
        *(__m128*)&value = _mm_and_ps( *(const __m128*)&value, *(const __m128*)&b.value);
    #else
        value &= b.value;
    #endif
        return *this;
    }
    c128& operator |= (const c128& b)
    {
    #ifdef __SSE2__
        *(__m128*)&value = _mm_or_ps( *(const __m128*)&value, *(const __m128*)&b.value);
    #else
        value |= b.value;
    #endif
        return *this;
    }
    c128& operator <<= (int nbits) { value <<= nbits; return *this; }
    c128& operator >>= (int nbits) { value >>= nbits; return *this; }

    c128 operator+ (const c128& b) const { return value + b.value; }
    c128 operator- (const c128& b) const { return value - b.value; }
    c128 operator^ (const c128& b) const
    {
    #ifdef __SSE2__
        return _mm_xor_ps( *(const __m128*)&value, *(const __m128*)&b.value);
    #else
        return value ^ b.value;
    #endif
    }
    c128 operator& (const c128& b) const
    {
    #ifdef __SSE2__
        return _mm_and_ps( *(const __m128*)&value, *(const __m128*)&b.value);
    #else
        return value & b.value;
    #endif
    }
    c128 operator| (const c128& b) const
    {
    #ifdef __SSE2__
        return _mm_or_ps( *(const __m128*)&value, *(const __m128*)&b.value);
    #else
        return value | b.value;
    #endif
    }
    c128 operator<< (int nbits) const { return value << nbits; }
    c128 operator>> (int nbits) const { return value >> nbits; }
    c128 operator~ () const { return ~value; }
};

c128 R128(const void* p)
{
  #ifdef LITTLE_ENDIAN_AND_UNALIGNED_ACCESS_OK
    return *(const uint128_t*)p;
  #else
    const unsigned char* data = (const unsigned char*)p;
    c128 res( R64(data) );
    c128 res2( R64(data + 8) );
    res |= res2 << 64;
    return res;
  #endif
}
static inline c128 RnSubstitute(const void* p, unsigned bytes)
{
    const unsigned char* data = (const unsigned char*)p;
    switch(bytes)
    {
        case 1: case 2: case 3: case 4:
        case 5: case 6: case 7: case 8:
            return Rn(p, bytes);
        case 16: return R128(p);
    }
    return c128(R64(data)) | (c128(Rn(data+8, bytes-8)) << 64);
}
#define Rn RnSubstitute

#endif // 128bit

newhash_t newhash_calc(const unsigned char* buf, unsigned long size)
{
    return newhash_calc_upd(0, buf, size);
}
newhash_t newhash_calc_upd(newhash_t c, const unsigned char* buf, unsigned long size)
{
#ifdef HUNDREDTWENTYEIGHTBIT_PLATFORM
    c128 c_cast = c; { c128 c = c_cast;
    unsigned long len = size;
    c128 a(UINT64_C(0x9e3779b97f4a7c15),UINT64_C(0xf39cc0605cedc834)); // 2^128 / ((1+sqrt(5))/2)
    c128 b(a);
    while(len >= 16*3)
    {
        a += (c128)R128(buf+0);
        b += (c128)R128(buf+16);
        c += (c128)R128(buf+32);
        mix128(a,b,c);
        buf += 48; len -= 48;
    }
    /*------------------------------------- handle the last 47 bytes */
    c = c + uint64_t(size);
    if(len >32)       { c += (c128)Rn(buf+31, len-31) & ~c128(0xFFu);
                        b += (c128)R128(buf+16);
                        a += (c128)R128(buf); }
    else if(len > 16) { b += (c128)Rn(buf+16,  std::min(16UL, len-16));
                        a += (c128)R128(buf); }
    else                a += (c128)Rn(buf, std::min(16UL, len));
    /* the first byte of c is reserved for the length */
    mix128(a,b,c);
    /*-------------------------------------------- report the result */
    return c.value; /* Note: this returns just the lowest 32 bits of the hash */
   }
#elif defined(SIXTY_BIT_PLATFORM)
    c64 c_cast = (uint64_t)c; { c64 c = c_cast;
    unsigned long len = size;
    c64 a(UINT64_C(0x9e3779b97f4a7c13)); // 2^64 / ((1+sqrt(5))/2)
    c64 b(a);
    while(len >= 8*3)
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
    while(len >= 4*3)
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
