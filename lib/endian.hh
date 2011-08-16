#ifndef bqtEndianHH
#define bqtEndianHH

#ifndef __STDC_CONSTANT_MACROS
#define __STDC_CONSTANT_MACROS /* for UINT16_C etc */
#endif

#include <stdint.h>

#if defined(__x86_64)||defined(__i386)
#define LITTLE_ENDIAN_AND_UNALIGNED_ACCESS_OK
#else
#undef LITTLE_ENDIAN_AND_UNALIGNED_ACCESS_OK
#endif

#ifdef WIN32
# define LL_FMT "I64"
#else
# define LL_FMT "ll"
#endif

// Rename the R8..R64 access functions, because
// some systems (such as Ubuntu 9.04 armel) define
// these names as enum fields in sys/context.h.
#define R8 Access_R8
#define R16 Access_R16
#define R16r Access_R16r
#define R24 Access_R24
#define R24r Access_R24r
#define R32 Access_R32
#define R32r Access_R32r
#define R64 Access_R64
#define R64r Access_R64r


static inline uint_fast16_t R8(const void* p)
{
    const unsigned char* data = (const unsigned char*)p;
    return data[0];
}
static inline uint_fast16_t R16(const void* p)
{
  #ifdef LITTLE_ENDIAN_AND_UNALIGNED_ACCESS_OK
    return *(const uint_least16_t*)p;
  #else
    const unsigned char* data = (const unsigned char*)p;
    return R8(data)  | (R8(data+1) << UINT16_C(8));
  #endif
}
static inline uint_fast16_t R16r(const void* p)
{
  #ifdef BIG_ENDIAN_AND_UNALIGNED_ACCESS_OK
    return *(const uint_least16_t*)p;
  #else
    const unsigned char* data = (const unsigned char*)p;
    return R8(data+1)  | (R8(data) << UINT16_C(8));
  #endif
}
static inline uint_fast32_t R24(const void* p)
{
    /* Note: This might be faster if implemented through R32 and a bitwise and,
     * but we cannot do that because we don't know if the third byte is a valid
     * memory location.
     */
    const unsigned char* data = (const unsigned char*)p;
    return R16(data) | (R8(data+2) << UINT32_C(16));
}
static inline uint_fast32_t R24r(const void* p)
{
    const unsigned char* data = (const unsigned char*)p;
    return R16(data+1) | (R8(data) << UINT32_C(16));
}
static inline uint_fast32_t R32(const void* p)
{
  #ifdef LITTLE_ENDIAN_AND_UNALIGNED_ACCESS_OK
    return *(const uint_least32_t*)p;
  #else
    const unsigned char* data = (const unsigned char*)p;
    return R16(data) | (R16(data+2) << UINT32_C(16));
  #endif
}
static inline uint_fast32_t R32r(const void* p)
{
  #ifdef BIG_ENDIAN_AND_UNALIGNED_ACCESS_OK
    return *(const uint_least32_t*)p;
  #else
    const unsigned char* data = (const unsigned char*)p;
    return R16(data+2) | (R16(data) << UINT32_C(16));
  #endif
}

#define L (uint_fast64_t)

static inline uint_fast64_t R64(const void* p)
{
  #ifdef LITTLE_ENDIAN_AND_UNALIGNED_ACCESS_OK
    return *(const uint_least64_t*)p;
  #else
    const unsigned char* data = (const unsigned char*)p;
    return (L R32(data)) | ((L R32(data+4)) << UINT64_C(32));
  #endif
}
static inline uint_fast64_t R64r(const void* p)
{
  #ifdef BIG_ENDIAN_AND_UNALIGNED_ACCESS_OK
    return *(const uint_least64_t*)p;
  #else
    const unsigned char* data = (const unsigned char*)p;
    return (L R32(data+4)) | ((L R32(data)) << UINT64_C(32));
  #endif
}

#undef L

static inline uint_fast64_t Rn(const void* p, unsigned bytes)
{
    const unsigned char* data = (const unsigned char*)p;
    uint_fast64_t res(0);
    switch(bytes)
    {
        case 8: return R64(p);
        case 4: return R32(p);
        case 2: return R16(p);
        case 7: res |= ((uint_fast64_t)R8(data+6)) << 48;
        case 6: res |= ((uint_fast64_t)R8(data+5)) << 40;
        case 5: res |= ((uint_fast64_t)R16(data+3)) << 24;
        case 3: res |= ((uint_fast64_t)R16(data+1)) << 8;
        case 1: res |= R8(data);
    }
    return res;
}

static void W8(void* p, uint_fast8_t value)
{
    unsigned char* data = (unsigned char*)p;
    data[0] = value;
}
static void W16(void* p, uint_fast16_t value)
{
  #ifdef LITTLE_ENDIAN_AND_UNALIGNED_ACCESS_OK
    *(uint_least16_t*)p = value;
  #else
    unsigned char* data = (unsigned char*)p;
    W8(data+0, value   );
    W8(data+1, value>>8);
  #endif
}
static void W24(void* p, uint_fast32_t value)
{
    unsigned char* data = (unsigned char*)p;
    W16(data+0, value);
    W8(data+2,  value >> UINT32_C(16));
}
static void W32(void* p, uint_fast32_t value)
{
  #ifdef LITTLE_ENDIAN_AND_UNALIGNED_ACCESS_OK
    *(uint_least32_t*)p = value;
  #else
    unsigned char* data = (unsigned char*)p;
    W16(data+0, value);
    W16(data+2, value >> UINT32_C(16));
  #endif
}
static void W64(void* p, uint_fast64_t value)
{
  #ifdef LITTLE_ENDIAN_AND_UNALIGNED_ACCESS_OK
    *(uint_least64_t*)p = value;
  #else
    unsigned char* data = (unsigned char*)p;
    W32(data+0, (value));
    W32(data+4, (value >> UINT64_C(32)));
  #endif
}

static inline void Wn(void* p, uint_fast64_t value, unsigned bytes)
{
    unsigned char* data = (unsigned char*)p;
    switch(bytes)
    {
        case 8: W64(p, value); break;
        case 7: W8(data+6, value>>48);
        case 6: W8(data+5, value>>40);
        case 5: W8(data+4, value>>32);
        case 4: W32(p, value); break;
        case 3: W24(p, value); break;
        case 2: W16(p, value); break;
        case 1: W8(p, value); break;
    }
}

#endif
