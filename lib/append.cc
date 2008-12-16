#include "append.hh"

const AppendInfo AnalyzeAppend
(
    const BoyerMooreNeedle& needle,
    size_t minimum_pos,
    size_t minimum_overlap,
    size_t overlap_granularity,

    const unsigned char* const haystack,
    size_t hlen
)
{
    AppendInfo append;
    append.OldSize = hlen;
    append.SetAppendPos(hlen, needle.size());

    /**** Find the appension position ****/

    if(minimum_pos < hlen)
    {
        size_t result;

        /* If the minimum_pos only allows appending (no complete overlaps possible) */
        if(unlikely(minimum_pos > hlen - std::min(hlen, needle.size())))
        {
            if(unlikely(overlap_granularity) == 0)
                result = hlen;
            else
                result = needle.SearchInWithAppendOnly(
                    haystack + minimum_pos,
                    hlen - minimum_pos,
                    minimum_overlap,
                    overlap_granularity) + minimum_pos;
        }
        else
        {
            result =
                needle.SearchInTurboWithAppend(
                    haystack + minimum_pos,
                    hlen - minimum_pos,
                    minimum_overlap,
                    overlap_granularity) + minimum_pos;
        }
        append.SetAppendPos(result, needle.size());
    }
    return append;
}

#if 0 /* Unused */

const size_t lr_match_length(const unsigned char* left,
                             const unsigned char* right,
                             size_t size)
{
    /* I believe std::memchr() might be better optimized
     * than std::find(). At least in glibc, memchr() does
     * does longword access on aligned addresses, whereas
     * find() (from STL) compares byte by byte. -Bisqwit
     */
    while(size > 0)
    {
        const unsigned char* leftptr = (const unsigned char*)
            std::memchr(left, right[0], size);

        if(!leftptr) break;

        if(std::memcmp(leftptr+1, &right[1], size-1) == 0)
        {
            return size;
        }

        size_t num_skip = leftptr-left+1;
        left  += num_skip;
        right += num_skip;
        size  -= num_skip;
    }
    return 0;
}

#endif
