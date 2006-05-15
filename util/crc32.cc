/*** CRC32 calculation (CRC::update) ***/
#include "crc32.h"

namespace
{
    class CRC
    {
        uint_least32_t crctable[256];
    public:
        CRC()
        {
            uint_fast32_t poly = 0xEDB88320LU;
            for(unsigned i=0; i<256; ++i)
            {
                uint_fast32_t crc = i;
                for(unsigned j=8; j-->0; ) { bool c=crc&1; crc>>=1; if(c)crc^=poly; }
                crctable[i] = crc;
            }
        }
        inline uint_fast32_t update(uint_fast32_t crc, unsigned char b) const
        {
            return ((crc >> 8) /* & 0x00FFFFFF*/) ^ crctable[(crc^b) & 0xFF];
        }
    } CRC32;
}

uint_fast32_t crc32_update(uint_fast32_t c, unsigned char b)
{
    return CRC32.update(c, b);
}

crc32_t crc32_calc(const unsigned char* buf, unsigned long size)
{
    uint_fast32_t value = crc32_startvalue;
    unsigned long pos = 0;
    while(size-- > 0) value = crc32_update(value, buf[pos++]);
    return value;
}
