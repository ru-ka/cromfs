#include <cstdio>
#include <cstdlib>
#include "../lib/boyermooreneedle.hh"
#include "../lib/stringsearchutil.cc"

static inline size_t reference_implementation(
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

static long rand2(long min, long max)
{
    return min + std::rand() % (max-min+1);
}

/*void asmtest()
{
    extern unsigned char*ptr1,*ptr2; extern size_t a,b,c,d;
    //a=reference_implementation(ptr1,ptr2,b,c,d);
    a=backwards_match_len(ptr1,ptr2,b,c,d);
}*/
unsigned char*ptr1,*ptr2; size_t a,b,c,d;

int main()
{
    std::srand(15);
    std::srand(15);

    unsigned char corpus[54000];

    const unsigned CorpusLength = sizeof(corpus);

    unsigned char matching_token[30];
    const unsigned TokenLength = sizeof(matching_token);

    for(unsigned a=0; a<CorpusLength; ++a)
        corpus[a] = std::rand() % 30;
    for(unsigned a=0; a<TokenLength; ++a)
        matching_token[a] = std::rand() % 6;

    /* Place the matching word into the corpus so that for each possible
     * alignment and length, it is found somewhere
     */
    unsigned basepos = 10;
    for(unsigned count=0; count<10; ++count)
    {
        for(unsigned align=0; align<16; ++align)
        {
            for(unsigned length = 1; length <= TokenLength; )
            {
                while((basepos % 16) != align) basepos += 1;

                if(basepos + length >= CorpusLength)
                {
                    fprintf(stderr, "Need more length than %u\n", basepos+length);
                    return -1;
                }

                std::memcpy(corpus+basepos, matching_token, length);

                basepos += length+1;

                if(length < 10) ++length; else
                if(length < 30) length += 3;
                else length += 11;
            }
        }
    }
    printf("Corpus length used: %u\n", basepos);

    unsigned n_errors = 0;

    //const unsigned num_tests = 200000000;
    const unsigned num_tests = 30000000;

    for(unsigned testno=0; testno<num_tests; ++testno)
    {
        unsigned pos1 = rand2(0, CorpusLength);
        unsigned pos2 = rand2(0, CorpusLength);

        const unsigned char* ptr1 = corpus + pos1;
        const unsigned char* ptr2 = corpus + pos2;

        unsigned strlen = std::min(CorpusLength-pos1, CorpusLength-pos2);

        unsigned minlen = rand2(0, CorpusLength);
        unsigned maxlen = strlen;//std::min(strlen, TokenLength);

#if 1 /* validity test */
        size_t result_ref =
            reference_implementation(ptr1,ptr2,strlen,maxlen,minlen);
        size_t result_new =
                 backwards_match_len_max_min(ptr1,ptr2,strlen,maxlen,minlen);

        if(result_ref != result_new)
        {
            fprintf(stderr, "Test %u (%u,%u,%u,%u,%u) -- ERROR: ref=%u, new=%u\n",
                testno,
                pos1,pos2,strlen,maxlen,minlen,
                (unsigned)result_ref, (unsigned)result_new);
            ++n_errors;
        }
        /*
        else
        {
            fprintf(stderr, "Test %u (%u,%u,%u,%u,%u) -- OK: ref=%u, new=%u\n",
                testno,
                pos1,pos2,strlen,maxlen,minlen,
                (unsigned)result_ref, (unsigned)result_new);
            ++n_errors;
        }
        */
#else /* performance test */
        volatile size_t result =
            //reference_implementation(ptr1,ptr2,strlen,maxlen,minlen);
            backwards_match_len(ptr1,ptr2,strlen,maxlen,minlen);
#endif
    }
    if(n_errors)
    {
        fprintf(stderr, "%u ERRORS\n", n_errors);
        printf("Backwards-match test NOT ok\n");
    }
    else
        printf("Backwards-match test ok\n");
}
