#ifndef bqtEndianHH
#define bqtEndianHH

#ifndef __STDC_CONSTANT_MACROS
#define __STDC_CONSTANT_MACROS /* for UINT16_C etc */
#endif

#include <stdint.h>

static inline uint_fast16_t R8(const void* p)
{
    const unsigned char* data = (const unsigned char*)p;
    return data[0];
}
static inline uint_fast16_t R16(const void* p)
{
    const unsigned char* data = (const unsigned char*)p;
    return R8(data)  | (R8(data+1) << UINT16_C(8));
}
static inline uint_fast32_t R24(const void* p)
{
    const unsigned char* data = (const unsigned char*)p;
    return R16(data) | (R8(data+2) << UINT32_C(16));
}
static inline uint_fast32_t R32(const void* p)
{
    const unsigned char* data = (const unsigned char*)p;
    return R16(data) | (R16(data+2) << UINT32_C(16));
}

#define L (uint_fast64_t)

static inline uint_fast64_t R64(const void* p)
{
    const unsigned char* data = (const unsigned char*)p;
    return (L R32(data)) | ((L R32(data+4)) << UINT64_C(32));
}

#undef L

static inline uint_fast64_t Rn(const void* p, unsigned bytes)
{
    switch(bytes)
    {
        case 1: return R8(p);
        case 2: return R16(p);
        case 3: return R24(p);
        case 4: return R32(p);
        case 8: return R64(p);
    }
    return 0;
}

static void W8(void* p, uint_fast8_t value)
{
    unsigned char* data = (unsigned char*)p;
    data[0] = value;
}
static void W16(void* p, uint_fast16_t value)
{
    unsigned char* data = (unsigned char*)p;
    W8(data+0, value   );
    W8(data+1, value>>8);
}
static void W24(void* p, uint_fast32_t value)
{
    unsigned char* data = (unsigned char*)p;
    W16(data+0, value);
    W8(data+2,  value >> UINT32_C(16));
}
static void W32(void* p, uint_fast32_t value)
{
    unsigned char* data = (unsigned char*)p;
    W16(data+0, value);
    W16(data+2, value >> UINT32_C(16));
}
static void W64(void* p, uint_fast64_t value)
{
    unsigned char* data = (unsigned char*)p;
    W32(data+0, (value));
    W32(data+4, (value >> UINT64_C(32)));
}

static inline void Wn(void* p, uint_fast64_t value, unsigned bytes)
{
    switch(bytes)
    {
        case 1: W8(p, value); break;
        case 2: W16(p, value); break;
        case 3: W24(p, value); break;
        case 4: W32(p, value); break;
        case 8: W64(p, value); break;
    }
}

#endif
