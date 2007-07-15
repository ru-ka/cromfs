/* 7zCrc.c */

#include "7zCrc.h"

#define kCrcPoly 0xEDB88320
UInt32 g_CrcTable[256];

#if 0 /* BISQWIT'S VERSION... (avoid duplicate code, refer to existing.) */

#include "../../crc32.h"
void MY_FAST_CALL CrcGenerateTable()
{
    unsigned i;
    for(i=0; i<256; ++i)
        g_CrcTable[i] = ...;
}
UInt32 MY_FAST_CALL CrcUpdate(UInt32 v, const void *data, size_t size)
{
    return crc32_calc_upd(v, data, size) ^ 0xFFFFFFFFULL;
}
UInt32 MY_FAST_CALL CrcCalc(const void *data, size_t size)
{
    return crc32_calc(data, size);
}
#else


void MY_FAST_CALL CrcGenerateTable()
{
  UInt32 i;
  for (i = 0; i < 256; i++)
  {
    UInt32 r = i;
    int j;
    for (j = 0; j < 8; j++)
      r = (r >> 1) ^ (kCrcPoly & ~((r & 1) - 1));
    g_CrcTable[i] = r;
  }
}

UInt32 MY_FAST_CALL CrcUpdate(UInt32 v, const void *data, size_t size)
{
  const Byte *p = (const Byte *)data;
  for (; size > 0 ; size--, p++) 
    v = CRC_UPDATE_BYTE(v, *p);
  return v;
}

UInt32 MY_FAST_CALL CrcCalc(const void *data, size_t size)
{
  return CrcUpdate(CRC_INIT_VAL, data, size) ^ 0xFFFFFFFF;
}

#endif
