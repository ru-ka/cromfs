#ifndef HHappendHH
#define HHappendHH

#include <stdint.h>

#include "boyermoore.hh"

/* AppendInfo describes how to append/overlap
 * the input into the given fblock.
 */
struct AppendInfo
{
    uint_fast32_t OldSize;           /* Size before appending */
    uint_fast32_t AppendBaseOffset;  /* Where to append */
    uint_fast32_t AppendedSize;      /* Size after appending */
public:
    AppendInfo() : OldSize(0), AppendBaseOffset(0), AppendedSize(0) { }
    void SetAppendPos(uint_fast32_t offs, uint_fast32_t datasize)
    {
        AppendBaseOffset = offs;
        AppendedSize     = std::max(OldSize, offs + datasize);
    }
};

const AppendInfo AnalyzeAppend(
    const BoyerMooreNeedle& needle,
    uint_fast32_t minimum_pos,
    long maximum_size,
    
    const unsigned char* data,
    uint_fast32_t datasize
);

#endif
