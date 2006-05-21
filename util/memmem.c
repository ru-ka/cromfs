#define _GNU_SOURCE

#include <string.h>
#include <stdio.h>

#include "memmem.h"

#include <limits.h>

static unsigned max_u(unsigned a, unsigned b)
{
    return a>b ? a : b;
}
static int max_i(int a, int b)
{
    return a>b ? a : b;
}
static int min_i(int a, int b)
{
    return a<b ? a : b;
}

/* memmem() implementation, GNU memmem.
 * Status: Works, and is slow.
 */
uint_fast32_t memmem_gnu_memmem
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

static int boyermoore_needlematch
    (const unsigned char* needle, uint_fast32_t nlen,
     uint_fast32_t portion,
     int_fast32_t offset)
{
    int_fast32_t virtual_begin = nlen-offset-portion;
    if(virtual_begin > 0)
    {
        /*
        printf("Comparing '%*s' and '%*s'\n",
            portion, needle+nlen-portion,
            portion, needle+virtual_begin);
        */
        return
            memcmp(needle + nlen - portion,
                   needle + virtual_begin,
                   portion) == 0
         && needle[virtual_begin-1] != needle[nlen-portion-1];
    }
    
    int_fast32_t ignore = -virtual_begin;
    return memcmp(needle + nlen - portion + ignore,
                  needle,
                  portion - ignore) == 0;
}

/* memmem() implementation, Boyer-Moore algorithm.
 * References:
 *   http://www-igm.univ-mlv.fr/~lecroq/string/node14.html
 *   http://www.inf.fh-flensburg.de/lang/algorithmen/pattern/bmen.htm
 *   http://en.wikipedia.org/wiki/Boyer-Moore_string_search_algorithm
 *
 * For some reason, this just doesn't work right.
 * Doesn't work. Grr. Hours, hours, hours wasted trying to get it work.
 * It does not work.
 */
static uint_fast32_t memmem_boyermoore
    (const unsigned char* __restrict__ haystack, uint_fast32_t hlen,
     const unsigned char* __restrict__ needle,   uint_fast32_t nlen) __attribute__((pure));
static uint_fast32_t memmem_boyermoore
    (const unsigned char* __restrict__ haystack, uint_fast32_t hlen,
     const unsigned char* __restrict__ needle,   uint_fast32_t nlen)
{
    int_fast32_t skip[nlen];
    int_fast32_t occ[UCHAR_MAX+1];

/*
    fprintf(stderr, "%d: hlen=%p:%u nlen=%p:%u\n", (int)__LINE__,
       haystack,(unsigned)hlen, needle,(unsigned)nlen);
*/

    /* Preprocess */
    if(1) /* scope: preprocess */
    {
        if(1) /* scope: init occ[]*/
        {
            uint_fast32_t a;
            
            /* Initialize the table to default value */
            for(a=0; a<UCHAR_MAX+1; ++a) occ[a] = -1;
            
            /* Then populate it with the analysis of the needle */
            for(a=0; a<nlen; ++a) occ[needle[a]] = a;
        }
        if(1) /* scope: init skip[] */
        {
            uint_fast32_t a;
            for(a=0; a<nlen; ++a)
            {
                int_fast32_t value = 0;
                while(value < nlen && !boyermoore_needlematch(needle, nlen, a, value))
                    ++value;
                skip[a] = value;
                //printf("skip[%u]: %u\n", a,skip[a]);

                /* This table seems to work just as wikipedia instructs
                 * it to work.
                 */
            }
        }
    }
    
    /* Search */
    if(1) /* scope: search */
    {
        uint_fast32_t hpos=0;
        
        /*
         This loop doesn't, mostly, work at all.
        */
        while(hpos <= hlen-nlen)
        {
            printf("Comparing at '%s'\n", haystack+hpos);
/*
            fprintf(stderr, "%d: hpos=%u hlen=%p:%u nlen=%p:%u\n", (int)__LINE__,
               (unsigned)hpos, haystack,(unsigned)hlen, needle,(unsigned)nlen);
*/
            uint_fast32_t npos=nlen-1;
            while(needle[npos] == haystack[npos+hpos])
            {
                if(npos == 0) return hpos;
                --npos;
            }
            
            printf("Mismatch at %d: good=%u, bad=%c=%d\n",
                npos,
                skip[npos],
                haystack[npos+hpos],
                occ[haystack[npos+hpos]]);
/*
            fprintf(stderr, "%d: hpos=%u npos=%u hlen=%u nlen=%u\n", (int)__LINE__,
               (unsigned)hpos, (unsigned)npos, (unsigned)hlen, (unsigned)nlen);
*/
            //hpos += max_i(0/*skip[npos]*/, npos-occ[haystack[npos+hpos]]);
            hpos += npos-occ[haystack[npos+hpos]];
        }
        printf("give up, hpos: %d:'%s'\n", hpos,haystack+hpos);
    }
    return hlen;
}

#if 0
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
            if(state < lim) return hpos;
        }
    }
    return hlen;
}
#endif

uint_fast32_t fast_memmem
    (const unsigned char* __restrict__ haystack, uint_fast32_t hlen,
     const unsigned char* __restrict__ needle, uint_fast32_t nlen)
{
#if 0
    /* gnu memmem() seems to be much slower than the memmem_boyermoore function */
    /* (By a factor of 3) */
    return mmemem_gnumemmem(haystack,hlen,needle,nlen);
#endif
#if 0
    /* This algorithm seems to be much slower than the boyer_moore algorithm */
    /* Therefore, it won't be used. */
    if(__builtin_expect( (long) (nlen <= CHAR_BIT * sizeof(unsigned long)) , 0l))
    {
        return memmem_shiftor(haystack, hlen, needle, (unsigned) nlen);
    }
#endif
    return memmem_boyermoore_simplified(haystack, hlen, needle, nlen);
}

#if 0
int main(void)
{
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
     );
}
#endif
