#include "sparsewrite.hh"

#include <algorithm>
#include <unistd.h>
#include <sys/types.h>

#ifdef __GNUC__
# define likely(x)       __builtin_expect(!!(x), 1)
# define unlikely(x)     __builtin_expect(!!(x), 0)
#else
# define likely(x)   (x)
# define unlikely(x) (x)
#endif

#include "simd.hh"
#ifdef __SSE__
# include <xmmintrin.h>
#endif
#ifdef __SSE2__
# include <emmintrin.h>
#endif
#ifdef __SSE4_1__
# include <smmintrin.h>
#endif

template<unsigned width, typename ptrtype>
static std::size_t size_until_align(ptrtype p)
{
    std::ptrdiff_t diff = std::ptrdiff_t(p) & std::ptrdiff_t(width-1);
    if(!diff) return 0;
    return std::ptrdiff_t(width) - diff;
}

template<std::size_t n>
static bool is_zero_lessthan4(const unsigned char* data)
{
    switch(n)
    {
        case 1: return !data[0];
        case 2: return !(data[0] | data[1]);
        case 3: return !(data[0] | data[1] | data[2]);
    }
    return true;
}

/*static bool is_zero_max3(const unsigned char* data, std::size_t n)
{
    switch(n)
    {
        case 1: return !data[0];
        case 2: return !(data[0] | data[1]);
        case 3: return !(data[0] | data[1] | data[2]);
    }
    return true;
}*/

static bool is_zero_block_align1(const unsigned char* data, std::size_t size)
{
    unsigned char res = 0;
    while(size--) res |= *data++;
    return res == 0;
}

static bool is_zero_max6(const unsigned char* data, std::size_t n)
{
    return is_zero_block_align1(data,n);
    /*
    switch(n)
    {
        case 1: return !data[0];
        case 2: return !(data[0] | data[1]);
        case 3: return !(data[0] | data[1] | data[2]);
        case 4: return !(data[0] | data[1] | data[2] | data[3]);
        case 5: return !(data[0] | data[1] | data[2] | data[3] | data[4]);
        case 6: return is_zero_lessthan4<3>(data) && is_zero_lessthan4<3>(data+3);
    }
    return true;
    */
}

template<std::size_t maxloops, std::size_t until_4>
static bool is_zero_block_align4(const unsigned char* data, std::size_t size)
{
    if(until_4+4 <= size)
    {
        if(!is_zero_lessthan4<until_4>(data)) return false;
        data += until_4; size -= until_4;

        if(maxloops == 0)
        {
            uint_fast32_t res = 0;
            do {
                res |= *(const uint_least32_t*) data; data += 4;
                size -= 4;
            } while(size >= 4);
            if(res)
                return false;
        }
        else
        {
            uint_fast32_t res = *(const uint_least32_t*)data;
            std::size_t ndiff = 4;
            if(maxloops >= 2 && size >= 8) { ndiff = 8;  res |= *(const uint_least32_t*)(data+4); }
            if(maxloops >= 3 && size >=12) { ndiff = 12; res |= *(const uint_least32_t*)(data+8); }
            if(maxloops >= 4 && size >=16) { ndiff = 16; res |= *(const uint_least32_t*)(data+8); }
            if(maxloops >= 5 && size >=20) { ndiff = 20; res |= *(const uint_least32_t*)(data+8); }
            if(maxloops >= 6 && size >=24) { ndiff = 24; res |= *(const uint_least32_t*)(data+8); }
            if(res) return false;
            data += ndiff; size -= ndiff;
        }
    }
    // Possible cases:
    // 01230123
    // x      |
    // xxx    |
    //  x     |
    //  xxxyyy| (biggest length=6, only byte units)
    //   x    |
    //   xxyyy|
    //    x   |
    //    xyyy|
    //     y  |
    //     yyy|
    //      y |
    //      yy|
    //       y|
    return is_zero_max6(data, size);
}

template<std::size_t n>
static bool is_zero_lessthan8(const unsigned char* data)
{
    if(!is_zero_lessthan4<n&3>(data)) return false;
    if(n >= 4)
    {
        data += n&3;
        if(*(const uint_least32_t*)data) return false;
    }
    return true;
}

/*static bool is_zero_max7(const unsigned char* data, std::size_t n)
{
    if(!is_zero_max3(data, n&3)) return false;
    if(n >= 4)
    {
        data += n&3;
        if(*(const uint_least32_t*)data) return false;
    }
    return true;
}

static bool is_zero_max14(const unsigned char* data, std::size_t n)
{
    if(!is_zero_max3(data, n&3)) return false;
    if(n >= 4)
    {
        data += n&3;
        uint_least32_t val = 0;
        val |= *(const uint_least32_t*)data;
        if(n >= 8)  val |= *(const uint_least32_t*)(data+4);
        if(n >= 12) val |= *(const uint_least32_t*)(data+8);
        if(val) return false;
    }
    return true;
}*/

#if defined(__MMX__) || defined(__x86_64) || defined(_M_X64)
static bool is_zero_block_align8_perfect(const unsigned char* data, std::size_t size)
{
    c64 res = 0;
    do {
        res |= c64( *(const uint64_t*) (data));
        data += 8; size -= 8;
    } while(size >= 8);
    return !res;
}

template<std::size_t maxloops, std::size_t until_8>
static inline bool is_zero_block_align8(const unsigned char* data, std::size_t size)
{
    if(until_8+8 <= size)
    {
        if(!is_zero_lessthan8<until_8>(data)) return false;
        data += until_8; size -= until_8;
        if(maxloops == 0)
        {
            if(!is_zero_block_align8_perfect(data, size)) return false;
            data += size&~7; size &= 7;
        }
        else
        {
            c64 res( *(const uint64_t*)data );
            std::size_t ndiff = 8;
            if(maxloops >= 2 && size >=16) { ndiff = 16; res |= c64(*(const uint64_t*)(data+8)); }
            if(maxloops >= 3 && size >=24) { ndiff = 24; res |= c64(*(const uint64_t*)(data+16)); }
            if(res) return false;
            data += ndiff; size -= ndiff;
        }
    }
    // Possible cases:
    // 0123456701234567
    // x              |
    // xxxxxxx        |
    //  x             |
    //  xxxzzzzppppyyy| (biggest length=14, max two u32s, no u64s)
    //    xzzzzppppyyy|

    //return is_zero_max14(data, size);
    return is_zero_block_align4<2,0> (data, size);
}

template<std::size_t n>
static bool is_zero_lessthan16(const unsigned char* data)
{
    if(!is_zero_lessthan8<n&7>(data)) return false;
    if(n >= 8)
    {
        data += n&7;
        if(c64(*(const uint64_t*)data)) return false;
    }
    return true;
}

/*static bool is_zero_max30(const unsigned char* data, std::size_t n)
{
    if(!is_zero_max7(data, n&7)) return false;
    if(n >= 8)
    {
        data += n&7;
        c64 val = 0;
        val |= c64(*(const uint64_t*)data);
        if(n >= 16) val |= c64(*(const uint64_t*)(data+8));
        if(n >= 24) val |= c64(*(const uint64_t*)(data+16));
        if(val) return false;
    }
    return true;
}
*/
#endif

#if defined(__SSE__) || defined(__SSE2__)
static bool is_zero_block_align16_perfect(const unsigned char* data, std::size_t size)
{
#ifdef __SSE2__
    __m128d res = _mm_setzero_pd();
    do {
        res = _mm_or_pd(res, *(const __m128d*)(data));
        data += 16; size -= 16;
    } while(size >= 16);
    union m128union { struct { c64::valuetype a, b; }; __m128d c; };
#else
    __m128 res = _mm_setzero_ps();
    do {
        res = _mm_or_ps(res, *(const __m128*)(data));
        data += 16; size -= 16;
    } while(size >= 16);
    union m128union { struct { c64::valuetype a, b; }; __m128 c; };
#endif
#if defined(__SSE4_1__)
  #ifdef __ICC
    __m128i tmp = (__m128i&)res;
  #else
    __m128i tmp = (__m128i)res;
  #endif
  #if (defined(__x86_64) || defined(_M_X64))
    if(_mm_extract_epi64(tmp, 0)
     | _mm_extract_epi64(tmp, 1)) return false;
  #else
//           #ifndef __MMX__
    if(_mm_extract_epi32(tmp, 0)
     | _mm_extract_epi32(tmp, 1)
    || _mm_extract_epi32(tmp, 2)
     | _mm_extract_epi32(tmp, 3)) return false;
//           #endif
  #endif
#else
    m128union& tmp = (m128union&)res;
    if( (c64(tmp.a) | c64(tmp.b)) )
        return false;
#endif
    return true;
}

template<std::size_t until_16>
static inline bool is_zero_block_align16(const unsigned char* data, std::size_t size)
{
    if(until_16+16 <= size)
    {
        if(!is_zero_lessthan16<until_16> (data)) return false;
        data += until_16; size -= until_16;
        if(!is_zero_block_align16_perfect(data, size)) return false;
        data += size&~15; size &= 15;
    }
    // Possible worst case:
    // 0123456789ABCDEF0123456789ABCDEF
    //  xxxzzzzyyyypppprrrrssssnnnnxxx  (length: 30, max two u64s, six u32s)
    //return is_zero_max30(data, size);
    //if( (until_16&7)+16 <= size)
    //    return is_zero_block_align8<2,0> (data, size);
    return is_zero_block_align4<0,0> (data, size);
}
#endif

bool is_zero_block(const unsigned char* data, std::size_t size)
{
#if 0
    /* easy implementation */
    for(std::size_t a = 0; a < size; ++a)
        if(data[a] != 0) return false;
    return true;

#else
 #if 0
    /* Memchr might use an algorithm optimized for aligned word-size access */
    return !std::memchr(Buffer, '\0', size);
 #else
    /* attempt of a faster implementation using aligned word access where possible */

#if defined(__SSE__) || defined(__SSE2__)
    #define p(n) case n: return is_zero_block_align16<n?16-n:0>(data, size);
    switch( unsigned(std::ptrdiff_t(data) & 15) )
    {
        p(0)  p(1)  p(2)  p(3)  p(4)  p(5)  p(6)  p(7)
        p(8)  p(9) p(10) p(11) p(12) p(13) p(14) p(15)
    }
    #undef p
#else
# if defined(__MMX__) || defined(__x86_64) || defined(_M_X64)
    #define p(n) case n: return is_zero_block_align8<0,n?8-n:0>(data, size);
    switch( unsigned(std::ptrdiff_t(data) & 7) )
    {
        p(0)  p(1)  p(2)  p(3)  p(4)  p(5)  p(6)  p(7)
    }
    #undef p
# else
    #define p(n) case n: return is_zero_block_align4<0,n?4-n:0>(data, size);
    switch( unsigned(std::ptrdiff_t(data) & 3) )
    {
        p(0)  p(1)  p(2)  p(3)
    }
    #undef p
# endif
#endif
    return is_zero_block_align1(data, size);
 #endif
#endif
}

bool SparseWrite(int fd,
    const unsigned char* Buffer,
    std::size_t BufSize,
    uint_fast64_t WritePos)
{
    /*fprintf(stderr, "Normally, would write %04"LL_FMT"X..%04"LL_FMT"X from %p\n",
        WritePos, WritePos+BufSize-1, Buffer);*/
#if 0
    return pwrite64(fd, Buffer, BufSize, WritePos) == (ssize_t)BufSize;
#else
    const std::size_t BlockSize = 1024;

    #define FlushBuf() do { \
        if(BufferedSize) \
        { \
             /*fprintf(stderr, "But writing %04"LL_FMT"X..%04"LL_FMT"X from %p\n", \
                 BufferedPos, BufferedPos+BufferedSize-1, BufferedBegin); */ \
            ssize_t res = pwrite64(fd, BufferedBegin, BufferedSize, BufferedPos); \
            if((std::size_t)res != BufferedSize) return false; \
        } \
        BufferedSize = 0; BufferedBegin = Buffer; BufferedPos = WritePos; \
    } while(0)

    #define GoAheadBy(n) do { Buffer += (n); BufSize -= (n); WritePos += (n); } while(0)

    #define SkipBuf(n) \
        do { SkippedSize += (n); GoAheadBy(n); } while(0)
    #define AppendBuf(n) \
        do { if(SkippedSize > 0) { FlushBuf(); SkippedSize = 0; } \
             BufferedSize += (n); GoAheadBy(n); } while(0)

    const unsigned char* BufferedBegin = Buffer;
    std::size_t BufferedSize = 0;
    uint_fast64_t BufferedPos  = WritePos;
    std::size_t SkippedSize = 0;

    {
    uint_fast64_t NextBlockBoundary = (WritePos + BlockSize-1) & uint_fast64_t(~BlockSize);
    uint_fast64_t UnalignedRemainder = std::min((uint_fast64_t)BufSize, NextBlockBoundary-WritePos);
    if(UnalignedRemainder) AppendBuf(UnalignedRemainder);
    }

    while(BufSize >= BlockSize)
        if(is_zero_block(Buffer, BlockSize))
            SkipBuf(BlockSize);
        else
            AppendBuf(BlockSize);

    if(BufSize > 0)
    {
        if(is_zero_block(Buffer, BufSize))
            SkipBuf(BufSize);
        else
            AppendBuf(BufSize);
    }
    FlushBuf();

    #undef AppendBuf
    #undef FlushBuf
    #undef SkipBuf
#endif
    return true;
}
