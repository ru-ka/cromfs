#include "append.hh"

const AppendInfo AnalyzeAppend
(
    const BoyerMooreNeedle& needle,
    uint_fast32_t minimum_pos,
    long maximum_size,
    
    const unsigned char* ptr,
    uint_fast32_t datasize
)
{
    AppendInfo append;
    append.OldSize = datasize;
    append.SetAppendPos(datasize, needle.size());
    
    /**** Find the appension position ****/

    if(!needle.empty() && append.OldSize > 0)
    {
        uint_fast32_t result = append.OldSize; /* By default, insert at end. */
        
        /* The maximum offset where we can search for a complete match
         * using an optimized algorithm.
         */
        int_fast32_t full_match_max = (long)append.OldSize - (long)needle.size();
        
        if(full_match_max >= (int_fast32_t)minimum_pos)
        {
            //std::fprintf(stderr, "full_match_max = %d\n", (int)full_match_max);
            
            /* +needle.size() because it is the number of bytes to search */
            uint_fast32_t res = minimum_pos + 
                needle.SearchIn(ptr + minimum_pos,
                                full_match_max + needle.size() - minimum_pos);
            if(res < full_match_max)
            {
                append.SetAppendPos(res, needle.size());
                return append;
            }
        }
        
        /* The rest of this algorithm checks for partial matches only */
        /* Though it _can_ check for complete matches too. */
        
        uint_fast32_t cap = std::min((long)append.OldSize, (long)(maximum_size - needle.size()));
        
        if(full_match_max < (int_fast32_t)minimum_pos) full_match_max = minimum_pos;
        for(uint_fast32_t a=full_match_max; a<cap; ++a)
        {
            /* I believe std::memchr() might be better optimized
             * than std::find(). At least in glibc, memchr() does
             * does longword access on aligned addresses, whereas
             * find() (from STL) compares byte by byte. -Bisqwit
             */
            //fprintf(stderr, "a=%u, cap=%u\n", (unsigned)a, (unsigned)cap);
            const unsigned char* refptr =
                (const unsigned char*)std::memchr(ptr+a, needle[0], cap-a);
            if(!refptr) break;
            a = refptr - ptr;
            unsigned compare_size = std::min((long)needle.size(), (long)(append.OldSize - a));
            
            /* compare 1 byte less because find() already confirmed the first byte */
            if(std::memcmp(refptr+1, &needle[1], compare_size-1) == 0)
            {
#if DEBUG_OVERLAP
                std::printf("\nOVERLAP: ORIG=%u, NEW=%u, POS=%u, COMPARED %u\n",
                    (unsigned)cap, (unsigned)needle.size(),
                    a, compare_size);
                for(unsigned b=0; b<4+compare_size; ++b)
                    std::printf("%02X ", ptr[cap - compare_size+b-4]);
                std::printf("\n");
                for(unsigned b=0; b<4; ++b) std::printf("   ");
                for(unsigned b=0; b<4+compare_size; ++b)
                    std::printf("%02X ", needle[b]);
                std::printf("\n");
#endif
                result = a; /* Put it here. */
                break;
            }
        }
        
        append.SetAppendPos(result, needle.size());
    }
    return append;
}
