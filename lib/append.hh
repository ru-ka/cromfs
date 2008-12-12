#ifndef HHappendHH
#define HHappendHH

#include "boyermooreneedle.hh"

/* AppendInfo describes how to append/overlap
 * the input into the given fblock.
 */
struct AppendInfo
{
    size_t OldSize;           /* Size before appending */
    size_t AppendBaseOffset;  /* Where to append */
    size_t AppendedSize;      /* Size after appending */
public:
    AppendInfo() : OldSize(0), AppendBaseOffset(0), AppendedSize(0) { }
    void SetAppendPos(size_t offs, size_t datasize)
    {
        AppendBaseOffset = offs;
        AppendedSize     = std::max(OldSize, offs + datasize);
    }
};

const AppendInfo AnalyzeAppend(
    const BoyerMooreNeedle& needle,
    size_t minimum_pos,
    size_t minimum_overlap,
    size_t overlap_granularity,

    const unsigned char* const data,
    size_t datasize
);

#endif
