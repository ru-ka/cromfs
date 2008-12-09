#ifdef __GNUC__
# define likely(x)       __builtin_expect(!!(x), 1)
# define unlikely(x)     __builtin_expect(!!(x), 0)
#else
# define likely(x)   (x)
# define unlikely(x) (x)
#endif

/* These are utility functions to be used in Boyer-Moore search (and others) */

#include "endian.hh"
#include "duffsdevice.hh"
#include "simd.hh"

#include <algorithm>
#include <cstring> // std::memchr, std::memcmp

#if defined(__SSE__) && defined(__GNUC__)
# include <xmmintrin.h>
#endif

namespace StringSearchUtil {
template<typename T>
inline bool MultiByteNeq(T a,T b) { return a!=b; }

/* - This does not work. comieq only compares the lower 32-bit values.
#if defined(__SSE__) && defined(__GNUC__)
template<>
inline bool MultiByteNeq(const __m128 a,
                         const __m128 b)
{
    //fprintf(stderr, "multibyteneq(%p,%p)\n",&a,&b); fflush(stderr);
    return _mm_comieq_ss(a,b) == 0;
}
#endif
*/

}

/*

Note: The simplest way to implement backwards_match_len() would be like this.
      However, we are desperately trying to create a faster implementation here.

static inline size_t Example_backwards_match_len(
    const unsigned char* ptr1,
    const unsigned char* ptr2,
    size_t strlen,
    size_t maxlen,
    size_t minlen)
{
    size_t result = minlen;
    while(result < maxlen && ptr1[strlen-(result+1)] == ptr2[strlen-(result+1)])
        ++result;
    return result;
}

*/


/* This function compares the two strings starting at ptr1 and ptr2,
 * which are assumed to be strlen bytes long, and returns a size_t
 * indicating how many bytes were identical, counting from the _end_
 * of both strings.
 */
template<bool NoMisalignment,
         typename WordType, typename Smaller1,
                            typename Smaller2,
                            typename Smaller3>
static inline size_t backwards_match_len_common_alignment(
    const unsigned char* const ptr1,
    const unsigned char* const ptr2,
    const size_t strlen,
    const size_t maxlen,
    const size_t minlen = 0)
{
    const size_t WordSize = sizeof(WordType);

    register size_t result = minlen;

    /* The Duff's device is used to optimize this function in many parts. */
    
    /* Comparison of a compile-time constant yields
     * conditional compilation, so this is good. */
    if(WordSize == 1)
    {
      #if 0
        int remain = maxlen-result, pos = strlen-maxlen-1;
        if(likely(remain > 0))
        {
            /*
            DuffsDevice3_once(remain%4, 
                if(ptr1[pos+remain] != ptr2[pos+remain]) return maxlen-remain;
                --remain; );*/
          #if defined(__x86_64) && 0
            while(remain >= 8)
            {
                remain -= 8;
                /*const uint_least64_t
                    *up1 = reinterpret_cast<const uint_least64_t*>(&ptr1[pos+remain+1]),
                    *up2 = reinterpret_cast<const uint_least64_t*>(&ptr2[pos+remain+1]);
                if(likely(*up1 != *up2))*/
                {
                    if(ptr1[pos+remain+8] != ptr2[pos+remain+8]) return maxlen-(remain+8);
                    if(ptr1[pos+remain+7] != ptr2[pos+remain+7]) return maxlen-(remain+7);
                    if(ptr1[pos+remain+6] != ptr2[pos+remain+6]) return maxlen-(remain+6);
                    if(ptr1[pos+remain+5] != ptr2[pos+remain+5]) return maxlen-(remain+5);
                    if(ptr1[pos+remain+4] != ptr2[pos+remain+4]) return maxlen-(remain+4);
                    if(ptr1[pos+remain+3] != ptr2[pos+remain+3]) return maxlen-(remain+3);
                    if(ptr1[pos+remain+2] != ptr2[pos+remain+2]) return maxlen-(remain+2);
                    if(ptr1[pos+remain+1] != ptr2[pos+remain+1]) return maxlen-(remain+1);
                }
            }
          #endif
            while(remain >= 4)
            {
                remain -= 4;
                /* Some platforms can't handle unaligned access,
                 * so this restricts the code to i386 and x86_64.
                 */
          #if defined(__x86_64) || defined(__i386)
                const uint_least32_t
                    *up1 = reinterpret_cast<const uint_least32_t*>(&ptr1[pos+remain+1]),
                    *up2 = reinterpret_cast<const uint_least32_t*>(&ptr2[pos+remain+1]);
                if(likely(*up1 != *up2))
          #endif
                {
                    if(ptr1[pos+remain+4] != ptr2[pos+remain+4]) return maxlen-(remain+4);
                    if(ptr1[pos+remain+3] != ptr2[pos+remain+3]) return maxlen-(remain+3);
                    if(ptr1[pos+remain+2] != ptr2[pos+remain+2]) return maxlen-(remain+2);
          #if ! ( defined(__x86_64) || defined(__i186) )
                    if(ptr1[pos+remain+1] != ptr2[pos+remain+1])
          #endif
                                                                 return maxlen-(remain+1);
                }
            }
            
            DuffsDevice3_once(remain, 
                if(ptr1[pos+remain] != ptr2[pos+remain]) return maxlen-remain;
                --remain; );
            
            return maxlen-remain;
        }
      #elif 0
        int remain = maxlen-result, pos = strlen-maxlen-1;
        DuffsDevice8(remain, remain > 0,
            if(ptr1[pos+remain] != ptr2[pos+remain]) goto exitduff;
            --remain;
           );
      exitduff: return maxlen-remain;
      #elif 0
        DuffsDevice8( (maxlen-result),
                      result < maxlen,
                      
                      if(ptr1[strlen-(result+1)] != ptr2[strlen-(result+1)])
                          return result; ++result;
                    );
      #else
        /* A generic implementation: */
        /* Also, fastest, for whatever reason */
        while(result < maxlen && ptr1[strlen-(result+1)] == ptr2[strlen-(result+1)])
            ++result;
      #endif
        return result;
    }
    
    if(unlikely(result >= maxlen)) return result;
    
    /* Attempts to compare WordSize bytes at time. */
    
    if(NoMisalignment)
    {
        /* First peel off the badly aligned bytes at the end */
      #if 0
        const size_t num_unaligned_at_end = (strlen-result) % (WordSize);
        
        /* This loop may be completely unrolled by compiler, so don't add duff's device here */
        for(size_t n=0; n<num_unaligned_at_end; ++n)
        {
            if(result >= maxlen || ptr1[strlen-(result+1)] != ptr2[strlen-(result+1)])
                return result;
            ++result;
        }
      #else
        while( (strlen-result) % (WordSize) )
        {
            if(result >= maxlen || ptr1[strlen-(result+1)] != ptr2[strlen-(result+1)])
                return result;
            ++result;
        }
      #endif
    }
    
    size_t aligned_anchor = (strlen-result) / WordSize;
    const WordType* ptr1_aligned = reinterpret_cast<const WordType*> (ptr1);
    const WordType* ptr2_aligned = reinterpret_cast<const WordType*> (ptr2);
    size_t num_aligned_match = 0;
    size_t max_aligned_match = (maxlen-result) / WordSize;
    
    DuffsDevice8( (max_aligned_match - num_aligned_match),
                  num_aligned_match < max_aligned_match,
                    
                  if( StringSearchUtil::
                    MultiByteNeq(ptr1_aligned[aligned_anchor-(num_aligned_match+1)],
                                 ptr2_aligned[aligned_anchor-(num_aligned_match+1)]) )
                    goto done_aligned_loop;
                  ++num_aligned_match;
                );
 done_aligned_loop: ;
 
    result += num_aligned_match * WordSize;

    return backwards_match_len_common_alignment<NoMisalignment,Smaller1,Smaller2,Smaller3,Smaller3>
        ( ptr1,ptr2, strlen, maxlen, result);
}

static inline size_t backwards_match_len(
    const unsigned char* ptr1,
    const unsigned char* ptr2,
    const size_t strlen,
    const size_t maxlen,
    const size_t minlen = 0)
{
    typedef uint_fast64_t ptr_int_type;
    
    /* Note: comparing uint_least16_t seems to always cause worse
     * performance.
     */
  
  #define AlignedAccessCheck(type1,type2,type3,type4) do { \
    const size_t offset1 = ( (ptr_int_type)ptr1 % sizeof(type1)); \
    const size_t offset2 = ( (ptr_int_type)ptr2 % sizeof(type1)); \
    if(unlikely(offset1 == offset2)) \
    { \
        const size_t offset = offset1; \
        return backwards_match_len_common_alignment<true, type1,type2,type3,type4> \
            (ptr1-offset, ptr2-offset, strlen+offset, maxlen, minlen); \
    } } while(0)
    
    /* Turns out any and all of these are slower than without. */
   #if defined(__SSE__) && defined(__GNUC__)
  //AlignedAccessCheck(__m128, uint_least32_t, uint_least8_t, uint_least8_t);
  //AlignedAccessCheck(__m128, uint_least8_t, uint_least8_t, uint_least8_t);
   #endif
  
  // 8 * (1/(8*8)), i.e. 1/8 chance of succeeding:
  //AlignedAccessCheck(uint_least64_t, uint_least32_t, uint_least8_t, uint_least8_t);
  
  // 4 * (1/(4*4)), i.e. 1/4 chance of succeeding:
  //AlignedAccessCheck(uint_least32_t, uint_least8_t, uint_least8_t, uint_least8_t);

  // 2 * (1/(2*2)), i.e. 1/2 chance of succeeding:
  //AlignedAccessCheck(uint_least16_t, uint_least8_t, uint_least8_t, uint_least8_t);
  
  #undef AlignedAccessCheck
  
  #if defined(__x86_64) && 0
    /* These methods ignore possible unoptimal alignment */
    #if 0 // slower than 32-bit-only
    return backwards_match_len_common_alignment<false,uint_least64_t,uint_least32_t,uint_least8_t,uint_least8_t>
        (ptr1,ptr2,strlen,maxlen,minlen);
    #elif 0 // slower than above
    return backwards_match_len_common_alignment<false,uint_least64_t,uint_least8_t,uint_least8_t,uint_least8_t>
        (ptr1,ptr2,strlen,maxlen,minlen);
    #else
    return backwards_match_len_common_alignment<false,uint_least32_t,uint_least8_t,uint_least8_t,uint_least8_t>
        (ptr1,ptr2,strlen,maxlen,minlen);
    #endif
  #elif defined(__i386)
    return backwards_match_len_common_alignment<false,uint_least32_t,uint_least8_t,uint_least8_t,uint_least8_t>
        (ptr1,ptr2,strlen,maxlen,minlen);
  #else
    /* Safe and generic method. */
    return backwards_match_len_common_alignment<false,uint_least8_t,uint_least8_t,uint_least8_t,uint_least8_t>
        (ptr1,ptr2,strlen,maxlen,minlen);
  #endif
}
static inline size_t backwards_match_len(
    const unsigned char* ptr1,
    const unsigned char* ptr2,
    size_t maxlen)
{
    return backwards_match_len(ptr1, ptr2, maxlen, maxlen);
}

static inline const unsigned char* ScanByte(
    const unsigned char* begin,
    const unsigned char byte,
    size_t n_bytes,
    const size_t granularity = 1)
{
    if(granularity == 1)
    {
      #if defined(__x86_64) && 0
        /* This is a port of the memchr algorithm 
         * employed in glibc-2.6.1/sysdeps/i386/memchr.S
         *      and in glibc-2.6.1/generic/memchr.c
         * But for unknown reason, it seems to perform
         * about 4 times worse than that.
         * So it is disabled.
         */
       #if 1
        {
            const size_t bad_alignment_size = ((uint_fast64_t)begin) % 8;
            // Peel off possible bad alignment
            size_t n_peel = std::min(bad_alignment_size, n_bytes);
            for(size_t n=0; n<n_peel; ++n)
                if(begin[n]==byte) return begin+n;
            n_bytes -= n_peel; begin += n_peel;
        }
       #endif
       #if 1
        uint_fast64_t fullword = byte * UINT64_C(0x0101010101010101);

        while(n_bytes >= 8)
        {
            const uint_fast64_t magic = UINT64_C(0xFEFEFEFEFEFEFEFF);
            const uint_fast64_t got_pattern
                = fullword ^ *reinterpret_cast<const uint_least64_t*> (begin);
            begin += 8;
            
            if(got_pattern + magic >= magic // test carry flag
            || (((got_pattern + magic) ^ got_pattern) | magic) != UINT64_C(-1))
            {/*
                const uint_fast32_t halfmagic = magic      ;// & UINT32_C(0xFFFFFFFF);
                const uint_fast32_t lowpat    = got_pattern;// & UINT32_C(0xFFFFFFFF);
                if( lowpat + halfmagic < UINT64_C(0x100000000)
                || (( (uint_least32_t)(lowpat + halfmagic) ^ lowpat) | halfmagic) != UINT32_C(-1))
                */  {
                    // It's in the low part
                    if(begin[-8+0]==byte) return begin-8+0;
                    if(begin[-8+1]==byte) return begin-8+1;
                    if(begin[-8+2]==byte) return begin-8+2;
                    if(begin[-8+3]==byte) return begin-8+3;
                    /*
                }
                {
                    // It's in the high part
                    */
                    if(begin[-8+4]==byte) return begin-8+4;
                    if(begin[-8+5]==byte) return begin-8+5;
                    if(begin[-8+6]==byte) return begin-8+6;
                    /*if(begin[-8+7]==byte)*/ return begin-8+7;
                }
            }
            n_bytes -= 8;
        }
      #endif
      
        size_t n=0;
        DuffsDevice8(n_bytes, n<n_bytes,
            if(begin[n] == byte) return begin+n; ++n; );
        return 0;
      #else
        return (const unsigned char*)std::memchr(begin, byte, n_bytes);
      #endif
    }

    while(n_bytes >= 1)
    {
        /*fprintf(stderr, "begin=%p, n_bytes=%u, granu=%u\n",
            begin,n_bytes,granularity);*/
        if(*begin == byte) return begin;
        
        if(n_bytes <= granularity) break;
        
        begin += granularity;
        n_bytes -= granularity;
    }
    return 0;
}
