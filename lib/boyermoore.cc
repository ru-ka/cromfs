#include <cstring>
#include "boyermoore.hh"
#include "threadfun.hh" // for InterruptibleContext
#include "stringsearchutil.hh"

#include "autodealloc.hh"

#include <cstdio>

#ifdef __GNUC__
# define likely(x)       __builtin_expect(!!(x), 1)
# define unlikely(x)     __builtin_expect(!!(x), 0)
#else
# define likely(x)   (x)
# define unlikely(x) (x)
#endif

namespace BoyerMooreSearch {

occtable_type InitOcc(const unsigned char* needle, const size_t needle_length)
{
    occtable_type result;
    InitOcc(result, needle, needle_length);
    return result;
}

void InitOcc(occtable_type& occ, const unsigned char* needle, const size_t needle_length)
{
    /* The initialization of occ[] corresponds to that
     * of bmBc at http://www-igm.univ-mlv.fr/%7Elecroq/string/node14.html.
     */

    for(unsigned a=0; a<=UCHAR_MAX; ++a)
        occ[a] = needle_length;

    if(unlikely(needle_length <= 1)) return;

    /* Populate it with the analysis of the needle */
    /* But ignoring the last letter */

    const size_t needle_length_minus_1 = needle_length-1;
   //#pragma omp for
    for(size_t a=0; a<needle_length_minus_1; ++a)
        occ[needle[a]] = needle_length_minus_1 - a;
}

skiptable_type
    InitSkip(const unsigned char* needle, const size_t needle_length)
{
    skiptable_type result(needle_length, needle_length);
    InitSkip(result, needle, needle_length);
    return result;
}


/* Note: this function expects skip to be resized to needle_length and initialized with needle_length. */
void InitSkip(skiptable_type& skip, const unsigned char* needle, const size_t needle_length)
{
    if(unlikely(needle_length <= 1)) return;

    /* I have absolutely no idea how this works. I just copypasted
     * it from http://www-igm.univ-mlv.fr/~lecroq/string/node14.html
     * and have since edited it in trying to seek clarity and efficiency.
     * In particular, the skip[] table creation is now interleaved within
     * building of the suff[] table, and the assumption that skip[] is
     * preinitialized into needle_length is utilized.
     * -Bisqwit
     */

    const size_t needle_length_minus_1 = needle_length-1;

    // Allocate this array from heap rather than stack.
    // Stack is faster, but fails if the array is very large.
    size_t* suff = new size_t[needle_length+1];
    autodealloc_array<size_t> suff_dealloc(suff);
    //std::vector<size_t> suff(needle_length+1);

    suff[needle_length] = needle_length;

    size_t j = 0; // index for writing into skip[]
    for(size_t f = needle_length-1, // Location of previously attempted match
               g = needle_length,   // Unknown purpose
               i = needle_length; --i > 0; ) // For each suffix length?
    {
        if(g < i)
        {
            // i > g.
            const size_t tmp = suff[i + f];
            if (i-g > tmp)
            {
                suff[i] = tmp;

                // Question: Does this "if" ever match?
                // If it doesn't, then this statement can be replaced with a continue.
                // Because I don't understand, I'm keeping it.
                // In tests, I cannot find a situation where it matches, though.
              #if 1
                if(unlikely(tmp == i))
                {
                    fprintf(stderr, "app1\n");

                    for(const size_t jlimit = needle_length - i; j < jlimit; ++j)
                        skip[j] = jlimit;
                }
              #endif
                continue;
            }
        }
        else
        {
            g = i; // if g >= i.
        }

        // At this line:    i >= g.
        f = needle_length - i;
        // Also this holds: f + g <= needle_length.

        const size_t match_len = backwards_match_len(
                needle,
                needle + f,
                g);

        g -= match_len;  // After this, 0 <= g <= i.

        suff[i] = i - g; // This is usually same as match_len but not always

        if(g == 0)
        {
            // This "if" matches sometimes. Less so on random data.
            // I think this only happens when the needle contains self-similarity.
            while(j < f) skip[j++] = f;
        }
    }

    //while(j < needle_length) skip[j++] = needle_length; -- not required; assumed to be already set so.

    for (size_t i = 0; i < needle_length_minus_1; ++i)
        skip[needle_length_minus_1 - suff[i + 1]] = needle_length_minus_1 - i;
}

size_t SearchInHorspool(const unsigned char* haystack, const size_t haystack_length,
    const occtable_type& occ,
    const unsigned char* needle, const size_t needle_length)
{
    if(unlikely(needle_length > haystack_length)) return haystack_length;

    InterruptibleContext make_interruptible;

    if(unlikely(needle_length == 1))
    {
        const unsigned char* result = (const unsigned char*)std::memchr(haystack, *needle, haystack_length);
        return result ? result-haystack : haystack_length;
    }

    const size_t needle_length_minus_1 = needle_length-1;

    const unsigned char last_needle_char = needle[needle_length_minus_1];
    const size_t search_room = haystack_length-needle_length;

    size_t hpos=0;
    while(hpos <= search_room)
    {
        const unsigned char occ_char = haystack[hpos + needle_length_minus_1];

        if(last_needle_char == occ_char
        && std::memcmp(needle, haystack+hpos, needle_length_minus_1) == 0)
        {
            return hpos;
        }

        hpos += occ[occ_char];
    }
    return haystack_length;
}

size_t SearchIn(const unsigned char* haystack, const size_t haystack_length,
    const occtable_type& occ,
    const skiptable_type& skip,
    const unsigned char* needle, const size_t needle_length)
{
    if(unlikely(needle_length > haystack_length)) return haystack_length;

    InterruptibleContext make_interruptible;

    if(unlikely(needle_length == 1))
    {
        const unsigned char* result =
            (const unsigned char*)std::memchr(haystack, *needle, haystack_length);
        return result ? result-haystack : haystack_length;
    }

    const size_t needle_length_minus_1 = needle_length-1;
    const size_t search_room = haystack_length-needle_length;
    const unsigned char last_needle_char = needle[needle_length_minus_1];

    size_t hpos=0;
    while(hpos <= search_room)
    {
        //fprintf(stderr, "haystack_length=%u needle_length=%u\n", hpos, needle_length);
        size_t match_len = 0;
        if(last_needle_char == haystack[hpos+needle_length_minus_1])
        {
            match_len = backwards_match_len(
                needle, haystack+hpos, needle_length);
            if(match_len == needle_length) return hpos;
        }

        const size_t mpos = needle_length_minus_1 - match_len;

        const unsigned char occ_char = haystack[hpos + mpos];

        const ssize_t bcShift = occ[occ_char] - match_len;
        const ssize_t gcShift = skip[mpos];

        size_t shift = std::max(gcShift, bcShift);

        hpos += shift;
    }
    return haystack_length;
}

size_t SearchInTurbo(const unsigned char* haystack, const size_t haystack_length,
    const occtable_type& occ,
    const skiptable_type& skip,
    const unsigned char* needle, const size_t needle_length)
{
    if(unlikely(needle_length > haystack_length)) return haystack_length;

    InterruptibleContext make_interruptible;

    if(unlikely(needle_length == 1))
    {
        const unsigned char* result =
            (const unsigned char*)std::memchr(haystack, *needle, haystack_length);
        return result ? result-haystack : haystack_length;
    }

    const size_t needle_length_minus_1 = needle_length-1;
    const size_t search_room = haystack_length-needle_length;

    size_t hpos = 0;
    size_t ignore_num = 0, shift = needle_length;
    const unsigned char last_needle_char = needle[needle_length_minus_1];

    /* For cache locality we reuse the "generic" backwards_match_len_max_min()
     * function here multiple times, even though we could use the simpler
     * backwards_match_len() and backwards_match_len_max() instead.
     */
    while(hpos <= search_room)
    {
        //fprintf(stderr, "haystack_length=%u needle_length=%u\n", hpos, needle_length);

        size_t match_len;
        if(ignore_num == 0)
        {
            match_len = 0;
            if(last_needle_char == haystack[hpos+needle_length_minus_1])
            {
                match_len = backwards_match_len_max_min(
                    needle, haystack+hpos, needle_length,
                    needle_length, /* maximum */
                    0 /* minimum */);
                if(match_len == needle_length) return hpos;
            }
        }
        else
        {
            match_len = 0;
            if(last_needle_char == haystack[hpos+needle_length_minus_1])
            {
                match_len =
                    backwards_match_len_max_min(
                        needle, haystack+hpos, needle_length,
                        shift, /* maximum */
                        0 /* minimum */
                       );
                if(match_len == shift) // it matched fully
                {
                    //if(shift + ignore_num >= needle_length) return hpos;
                    match_len =
                        backwards_match_len_max_min(
                            needle, haystack+hpos, needle_length,
                            needle_length, /* maximum */
                            shift + ignore_num /* minimum */
                        );
                }
                if(match_len >= needle_length) return hpos;
            }
        }

        const size_t mpos = needle_length_minus_1 - match_len;

        const unsigned char occ_char = haystack[hpos + mpos];

        const ssize_t bcShift = occ[occ_char] - match_len;
        const size_t gcShift  = skip[mpos];
        const ssize_t turboShift = ignore_num - match_len;

        ssize_t tmp = gcShift;
        if(bcShift    > tmp) tmp = bcShift;
        if(turboShift > tmp) tmp = turboShift;
        shift = tmp;

        if(shift == gcShift)
            ignore_num = std::min( needle_length - shift, match_len);
        else
        {
            if(turboShift < bcShift && ignore_num >= shift)
                shift = ignore_num + 1;
            ignore_num = 0;
        }
        hpos += shift;
    }
    return haystack_length;
}

} // namespace
