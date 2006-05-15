#ifdef __cplusplus
extern "C" {
#endif

#include <stdint.h>

typedef uint_least32_t crc32_t;

#define crc32_startvalue 0xFFFFFFFFUL

extern crc32_t crc32_update(crc32_t c, unsigned char b);
extern crc32_t crc32_calc(const unsigned char* buf, unsigned long size);

#ifdef __cplusplus
}
#endif
