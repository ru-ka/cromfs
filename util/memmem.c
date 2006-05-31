#define _GNU_SOURCE

#include <string.h>
#include <stdio.h>

#include "memmem.h"

#include <limits.h>

static uint_fast32_t max_u(uint_fast32_t a, uint_fast32_t b) __attribute__((pure));
static uint_fast32_t max_u(uint_fast32_t a, uint_fast32_t b)
{
    return a>b ? a : b;
}
static int_fast32_t max_i(int_fast32_t a, int_fast32_t b) __attribute__((pure));
static int_fast32_t max_i(int_fast32_t a, int_fast32_t b)
{
    return a>b ? a : b;
}
static int_fast32_t min_i(int_fast32_t a, int_fast32_t b) __attribute__((pure));
static int_fast32_t min_i(int_fast32_t a, int_fast32_t b)
{
    return a<b ? a : b;
}

/* memmem() implementation, GNU memmem.
 * Status: Works, and is slow.
 */
static uint_fast32_t memmem_gnu_memmem
    (const unsigned char* haystack, uint_fast32_t hlen,
     const unsigned char* needle, uint_fast32_t nlen)
{
    const unsigned char*p = memmem(haystack,hlen, needle,nlen);
    return p ? (uint_fast32_t)(p-haystack) : hlen;
}


/* memmem() implementation, Boyer-Moore algorithm, simplified version.
 * Reference: http://www.ntecs.de/old-hp/uu9r/lang/html/cplusplus.en.html
 * Status: Works, and is fast.
 */
static uint_fast32_t memmem_boyermoore_simplified
    (const unsigned char* haystack, uint_fast32_t hlen,
     const unsigned char* needle,   uint_fast32_t nlen) __attribute__((pure));
static uint_fast32_t memmem_boyermoore_simplified
    (const unsigned char* haystack, uint_fast32_t hlen,
     const unsigned char* needle,   uint_fast32_t nlen)
{
    uint_fast32_t skip[UCHAR_MAX+1];
    
    /* Preprocess */
    if(1) /* for scope */
    {
        uint_fast32_t a, b;
        
        /* Initialize the table to default value */
        for(a=0; a<UCHAR_MAX+1; ++a) skip[a] = nlen;
        
        /* Then populate it with the analysis of the needle */
        for(a=0, b=nlen; a<nlen; ++a) skip[needle[a]] = --b;
    }
    
    /* Search */
    if(1) /* for scope */
    {
        /* Start searching from the end of needle (this is not a typo) */
        uint_fast32_t hpos = nlen-1;
        while(hpos < hlen)
        {
            /* Compare the needle backwards, and stop when first mismatch is found */
            uint_fast32_t npos = nlen-1;
            
            while(haystack[hpos] == needle[npos])
            {
                if(npos == 0) return hpos;
                --hpos;
                --npos;
            }
            
            /* Find out how much ahead we can skip based
             * on the byte that was found
             */
            if(1) /* for scope */
            {
                hpos += max_u(nlen-npos, skip[haystack[hpos]]);
            }
        }
        return hpos;
    }
}

/* This helper function checks, whether the last "portion" bytes
 * of "needle" (which is "nlen" bytes long) exist within the "needle"
 * at offset "offset" (counted from the end of the string),
 * and whether the character preceding "offset" is not a match.
 * Notice that the range being checked may reach beyond the
 * beginning of the string. Such range is ignored.
 */
static int boyermoore_needlematch
    (const unsigned char* needle, uint_fast32_t nlen,
     uint_fast32_t portion, uint_fast32_t offset) __attribute__((pure));
static int boyermoore_needlematch
    (const unsigned char* needle, uint_fast32_t nlen,
     uint_fast32_t portion, uint_fast32_t offset)
{
    int_fast32_t virtual_begin = nlen-offset-portion;
    int_fast32_t ignore = 0;
    if(virtual_begin < 0) { ignore = -virtual_begin; virtual_begin = 0; }
    
    if(virtual_begin > 0 && needle[virtual_begin-1] == needle[nlen-portion-1])
        return 0;

    return
        memcmp(needle + nlen - portion + ignore,
               needle + virtual_begin,
               portion - ignore) == 0;
}

/* memmem() implementation, Boyer-Moore algorithm.
 * References:
 *   http://www-igm.univ-mlv.fr/~lecroq/string/node14.html
 *   http://www.inf.fh-flensburg.de/lang/algorithmen/pattern/bmen.htm
 *   http://en.wikipedia.org/wiki/Boyer-Moore_string_search_algorithm
 * Status: Fast, and probably works now.
 */
static uint_fast32_t memmem_boyermoore
    (const unsigned char* __restrict__ haystack, uint_fast32_t hlen,
     const unsigned char* __restrict__ needle,   uint_fast32_t nlen) __attribute__((pure));
static uint_fast32_t memmem_boyermoore
    (const unsigned char* __restrict__ haystack, uint_fast32_t hlen,
     const unsigned char* __restrict__ needle,   uint_fast32_t nlen)
{
    uint_fast32_t skip[nlen];
    int_fast32_t occ[UCHAR_MAX+1];

    /* Preprocess */
    if(1) /* scope: preprocess */
    {
        if(1) /* scope: init occ[]*/
        {
            uint_fast32_t a;
            
            /* Initialize the table to default value */
            for(a=0; a<UCHAR_MAX+1; ++a) occ[a] = -1;
            
            /* Then populate it with the analysis of the needle */
            /* But ignoring the last letter */
            for(a=0; a<nlen-1; ++a) occ[needle[a]] = a;
        }
        if(1) /* scope: init skip[] */
        {
#if 0 /* Bisqwit's method */
            /* Complexity: O(nlen^3) ... oops */
            uint_fast32_t a;
            for(a=0; a<nlen; ++a)
            {
                uint_fast32_t value = 0;
                while(value < nlen && !boyermoore_needlematch(needle, nlen, a, value))
                    ++value;
                skip[nlen-a-1] = value;
                //printf("skip[%u]: %u\n", a,skip[a]);

                /* This table seems to work just as wikipedia instructs
                 * it to work.
                 */
            }
#else /* method from http://www-igm.univ-mlv.fr/~lecroq/string/node14.html */
            /* Seems to be considerably faster than Bisqwit's method */
            /* Though I have absolutely no idea what it does */
            int_fast32_t suff[nlen];
            if(1) /* scope */
            {
                int_fast32_t i, f=0, g=nlen-1;
                suff[g] = nlen;
                for (i = g; i-- > 0; )
                {
                    if (i > g && suff[i + nlen - 1 - f] < i - g)
                        suff[i] = suff[i + nlen - 1 - f];
                    else
                    {
                        f = i;
                        g = min_i(g, i);
                        while (g >= 0 && needle[g] == needle[g + nlen - 1 - f])
                            --g;
                        suff[i] = f - g;
                    }
                }
            }
            if(1) /* another scope */
            {
                uint_fast32_t j=0;
                int_fast32_t i;
                for (i = 0; i < nlen; ++i) skip[i] = nlen;
                for (i = nlen - 1; i >= -1; --i)
                    if (i == -1 || suff[i] == i + 1)
                        for (; j < nlen - 1 - i; ++j)
                            if (skip[j] == nlen)
                                skip[j] = nlen - 1 - i;
                for (i = 0; i <= nlen - 2; ++i)
                    skip[nlen - 1 - suff[i]] = nlen - 1 - i;
            }
#endif
        }
    }
    
    /* Search */
    if(1) /* scope: search */
    {
#if 1
        uint_fast32_t hpos=0;
        while(hpos <= hlen-nlen)
        {
            uint_fast32_t npos=nlen-1;
            while(needle[npos] == haystack[npos+hpos])
            {
                if(npos == 0) return hpos;
                --npos;
            }
            hpos += max_i(skip[npos], npos - occ[haystack[npos+hpos]]);
        }
#else /* Turbo BM */
        /* Doesn't seem to give the same results as the normal method
         * - Therefore, can't use
         */
        uint_fast32_t hpos=0;
        uint_fast32_t processed_prev=0;
        int shift=nlen;
        while (hpos <= hlen-nlen)
        {
            uint_fast32_t npos=nlen-1;
            while (needle[npos] == haystack[npos + hpos])
            {
                if(npos == 0) return hpos;
                --npos;
                if (processed_prev != 0 && npos == nlen - 1 - shift)
                    npos -= processed_prev;
            }
            
            if(1) /* scope */
            {
                uint_fast32_t processed_now = nlen-1 - npos;
                int turboShift = processed_prev - processed_now;
                int bcShift = npos - occ[haystack[npos+hpos]];
                shift = max_i(turboShift, bcShift);
                shift = max_i(shift, skip[npos]);
                if (shift == skip[npos])
                    processed_prev = min_i(nlen - shift, processed_now);
                else
                {
                    if (turboShift < bcShift)
                        shift = max_i(shift, processed_prev + 1);
                    processed_prev = 0;
                }
            }
            hpos += shift;
        }
#endif
    }
    return hlen;
}

/* memmem() implementation, Shift-Or algorithm.
 * Reference: http://en.wikipedia.org/wiki/Shift_Or_Algorithm
 * Status: Works, but slow.
 */
static uint_fast32_t memmem_shiftor
    (const unsigned char* __restrict__ haystack, uint_fast32_t hlen,
     const unsigned char* __restrict__ needle,   unsigned nlen) __attribute__((pure));
static uint_fast32_t memmem_shiftor
    (const unsigned char* __restrict__ haystack, uint_fast32_t hlen,
     const unsigned char* __restrict__ needle,   unsigned nlen)
{
    unsigned long S[UCHAR_MAX+1];
    unsigned long lim;
    
    /* Preprocess */
    lim=0;
    if(1) /* for scope */
    {
        unsigned i;
        unsigned long j;

        for(i=0; i<UCHAR_MAX+1; ++i) S[i] = ~0UL;

        for(i=0, j=1; i<nlen; ++i, j<<=1)
        {
            S[needle[i]] &= ~j;
            lim |= j;
        }
    }
    lim = ~(lim >> 1);
    
    /* Search */
    if(1)
    {
        unsigned long state = ~0UL;
        uint_fast32_t hpos;
        for(hpos = 0; hpos < hlen; ++hpos)
        {
            state = (state << 1) | S[haystack[hpos]];
            if(state < lim) return hpos - nlen + 1;
        }
    }
    return hlen;
}

uint_fast32_t fast_memmem
    (const unsigned char* __restrict__ haystack, uint_fast32_t hlen,
     const unsigned char* __restrict__ needle, uint_fast32_t nlen)
{
/*
    fprintf(stderr, "%d: hlen=%p:%u nlen=%p:%u\n", (int)__LINE__,
       haystack,(unsigned)hlen, needle,(unsigned)nlen);
*/

#if 0
    /* gnu memmem() seems to be much slower than the memmem_boyermoore function */
    /* (By a factor of 3) */
    return memmem_gnu_memmem(haystack,hlen,needle,nlen);
#endif
#if 0
    /* Although shift-or is faster than gnu_memmem, it's slower
     * than boyermoore (both the vanilla and simplified versions).
     * (Tested on 64-byte blocks on 64-bit system.)
     */
    if(__builtin_expect( (long) (nlen <= CHAR_BIT * sizeof(unsigned long)) , 0l))
    {
        return memmem_shiftor(haystack, hlen, needle, (unsigned) nlen);
    }
#endif
    if(__builtin_expect( (nlen == 1), 0l )) return memchr(haystack, *needle, hlen);
    return memmem_boyermoore(haystack, hlen, needle, nlen);
}

#if 0
int main(void)
{
    unsigned a;
    for(a=0; a<1; ++a) {
/*
    const char* input = "here is a simple example, yeah";
    const char* key = "example";
*/
/**/
    const char* input = "kissassassaessassakauppa";
    const char* key   =            "essassa";
/**/
/*
    const char* input = "abaababacba";
    const char* key   = "abbabab";
*/
    
    printf("'%s'\n",
    input + fast_memmem(input, strlen(input), key, strlen(key))
     )
     ;
    }
}
#endif
