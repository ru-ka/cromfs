/*** CRC32 calculation (CRC::update) ***/
#include "crc32.h"

#ifdef __GNUC__
# define likely(x)       __builtin_expect(!!(x), 1)
# define unlikely(x)     __builtin_expect(!!(x), 0)
#else
# define likely(x)   (x)
# define unlikely(x) (x)
#endif

namespace
{
    static class CRC
    {
        uint_least32_t crctable[256];
    public:
        CRC()
        {
            uint_fast32_t poly = 0xEDB88320LU;
            for(unsigned i=0; i<256; ++i)
            {
                uint_fast32_t crc = i;
                for(unsigned j=8; j-->0; )
                    crc = (crc >> 1) ^ ( (crc&1) ? poly : 0 );
                crctable[i] = crc;
            }
        }
        inline uint_fast32_t update(uint_fast32_t crc, unsigned char b) const
/*
            __attribute__((pure))
*/
        {
            return ((crc >> 8) /* & 0x00FFFFFF*/) ^ crctable[(unsigned char)(crc^b)];
        }
    } CRC32;
}

uint_fast32_t crc32_update(uint_fast32_t c, unsigned char b)
{
    return CRC32.update(c, b);
}

crc32_t crc32_calc(const unsigned char* buf, unsigned long size)
{
    return crc32_calc_upd(crc32_startvalue, buf, size);
}
crc32_t crc32_calc_upd(crc32_t c, const unsigned char* buf, unsigned long size)
{
    uint_fast32_t value = c;

#if 0
    unsigned long pos = 0;
    while(size-- > 0) value = CRC32.update(value, buf[pos++]);
#endif

#if 0
    for(unsigned long p=0; p<size; ++p) value = CRC32.update(value, buf[p]);
#endif

#if 1
    unsigned unaligned_length = (4 - (unsigned long)(buf)) & 3;
    if(size < unaligned_length) unaligned_length = size;
    switch(unaligned_length)
    {
        case 3: value = CRC32.update(value, *buf++);
        case 2: value = CRC32.update(value, *buf++);
        case 1: value = CRC32.update(value, *buf++);
                size -= unaligned_length;
        case 0: break;
    }
    for(; size >= 4; size -= 4, buf += 4)
    {
        value = CRC32.update(value, buf[0]);
        value = CRC32.update(value, buf[1]);
        value = CRC32.update(value, buf[2]);
        value = CRC32.update(value, buf[3]);
    }
    switch(size)
    {
        case 3: value = CRC32.update(value, *buf++);
        case 2: value = CRC32.update(value, *buf++);
        case 1: value = CRC32.update(value, *buf++);
        case 0: break;
    }
#endif

#if 0 /* duff's device -- no gains observed over the simple loop above */
    if(__builtin_expect( (size!=0), 1l ))
    {
        { if(__builtin_expect( !(size&1), 1l )) goto case_0;
          --buf; goto case_1;
        }
        //switch(size % 2)
        {
            //default: 
                 do { size -= 2; buf += 2;
            case_0: value = CRC32.update(value, buf[0]);
            case_1: value = CRC32.update(value, buf[1]);
                    } while(size > 2);
        }
    }
#endif

#if 0
    while(size-- > 0) value = CRC32.update(value, *buf++);
#endif
    
    return value;
}
