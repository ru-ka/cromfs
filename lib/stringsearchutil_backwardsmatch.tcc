/*

Depending on circumstances, this code is where most of the time of mkcromfs
is spent. Therefore it must be optimized as well as possible.

*/


    const unsigned char* ptr1_end = ptr1 + (strlen-minlen);
    const unsigned char* ptr2_end = ptr2 + (strlen-minlen);

    size_t result = minlen;

    typedef size_t WordType;
    enum { WordSize = sizeof(WordType) };

    if(unlikely(result > maxlen)) return result;

#if 1
    /* ICC creates really optimal assembler code for this C++ code on INTEL64. */
    if(result+WordSize <= maxlen)
    {
        const size_t num_bytes_fast = (maxlen-minlen) / WordSize * WordSize;
        ptr1_end -= num_bytes_fast;
        ptr2_end -= num_bytes_fast;
        result   += num_bytes_fast;
        const size_t num_words_fast = (maxlen-minlen) / WordSize;
        //fprintf(stderr, "offs %lu\n", offset);
        for(size_t offset=num_words_fast; ; )
        {
            //__label__ aligntag __attribute__((align(16)));  aligntag:; -- alas, this syntax is not supported.

            /* Note: Potential misalignment penalty here.
             * But intel_fast_memcmp also does this,
             * so I guess it doesn't matter that much.
             */
            if(( *(const WordType*)&ptr1_end[(offset-1)*WordSize]
             !=  *(const WordType*)&ptr2_end[(offset-1)*WordSize] ))
            {
                result -= (offset*WordSize); // how much has matched for certain
                // Rely on compiler unrolling the loop here:
                for(size_t n=1; n<WordSize; ++n)
                    if(ptr1_end[offset*WordSize-n] != ptr2_end[offset*WordSize-n])
                        return result+n-1;
                return result+(WordSize-1);
            }
            if(--offset <= 0) break;
        }
    }
    //assert(result+WordSize > maxlen);
#endif

    /* A generic implementation: */
    //ptr1_end = ptr1 + strlen-maxlen;
    //ptr2_end = ptr2 + strlen-maxlen;
    size_t n = (maxlen-result);
    ptr1_end -= n;
    ptr2_end -= n;
    // The maximum iteration count for this loop is WordSize.
#if 1
    if(WordSize > sizeof(int)
    && n >= sizeof(int)
    && *(const int*)&ptr1_end[n-sizeof(int)] == *(const int*)&ptr2_end[n-sizeof(int)])
        n -= sizeof(int);
#endif
    for(; n > 0; --n)
        if(ptr1_end[n-1] != ptr2_end[n-1])
            return maxlen-n;
    return maxlen;

/*
    while(result < maxlen && *--ptr1_end == *--ptr2_end)
        ++result;

    //while(result < maxlen && ptr1_end[-1-result] == ptr2_end[-1-result])
    //    ++result;
    return result;
*/
