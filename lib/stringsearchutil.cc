#include "stringsearchutil.hh"

#ifdef __GNUC__
# define likely(x)       __builtin_expect(!!(x), 1)
# define unlikely(x)     __builtin_expect(!!(x), 0)
#else
# define likely(x)   (x)
# define unlikely(x) (x)
#endif

/* These are utility functions to be used in Boyer-Moore search (and others) */

#include "endian.hh"

#include <algorithm>
#include <cstring> // std::memchr, std::memcmp

#include "duffsdevice.hh"

//#include <assert.h>
/*
#include "simd.hh"

#if defined(__SSE__) && defined(__GNUC__)
# include <xmmintrin.h>
#endif
*/
namespace StringSearchUtil {
/*
template<typename T>
inline bool MultiByteNeq(T a,T b) { return a!=b; }
*/

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
 *
 * @ptr1:  The first string
 * @ptr2:  The second string
 * @strlen:
 *   Length of data in both @ptr1 and @ptr2.
 *   Access beyond this length is not allowed.
 * @maxlen:
 *   Maximum match length we're interested of.
 *   It must be that 0 <= @maxlen <= @strlen.
 */
size_t backwards_match_len_max_min(
    const unsigned char* const ptr1,
    const unsigned char* const ptr2,
    size_t strlen,
    size_t maxlen,
    size_t minlen
)
{
# include "stringsearchutil_backwardsmatch.tcc"
}

size_t backwards_match_len_max(
    const unsigned char* const ptr1,
    const unsigned char* const ptr2,
    size_t strlen,
    size_t maxlen
)
{
#define minlen 0
# include "stringsearchutil_backwardsmatch.tcc"
#undef minlen
}

size_t backwards_match_len(
    const unsigned char* const ptr1,
    const unsigned char* const ptr2,
    size_t strlen
)
{
#define minlen 0
#define maxlen strlen
# include "stringsearchutil_backwardsmatch.tcc"
#undef maxlen
#undef minlen
}

/* This function compares the two strings starting at ptr1 and ptr2,
 * which are assumed to be strlen bytes long, and returns a size_t
 * indicating how many bytes were identical, counting from the _end_
 * of both strings.
 *
 * @ptr1:  The first string
 * @ptr2:  The second string
 * @strlen:
 *   Length of data in both @ptr1 and @ptr2.
 *   Access beyond this length is not allowed.
 * @maxlen:
 *   Maximum match length we're interested of.
 *   It must be that 0 <= @maxlen <= @strlen.
 * @minlen:
 *   Number of bytes from the end of the string
 *   that we assume it is indeed matching.
 *   It must be that 0 <= @minlen <= @maxlen.
 */
/*size_t backwards_match_len(
    const unsigned char* const ptr1,
    const unsigned char* const ptr2,
    const size_t strlen,
    const size_t maxlen,
    const size_t minlen
)
{
    if(unlikely(minlen >= maxlen)) return minlen;
    return minlen + backwards_match_len(ptr1,ptr2, strlen-minlen, maxlen-minlen);
}*/

const unsigned char* ScanByte(
    const unsigned char* begin,
    const unsigned char byte,
    size_t n_bytes,
    const size_t granularity)
{
    if(likely(granularity == 1))
    {
        return (const unsigned char*)std::memchr(begin, byte, n_bytes);
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
