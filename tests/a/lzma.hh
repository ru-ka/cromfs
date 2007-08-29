#ifndef HHlzmaHH
#define HHlzmaHH

#include <vector>

extern int LZMA_verbose;

extern unsigned LZMA_NumFastBytes;
extern unsigned LZMA_AlgorithmNo;
extern unsigned LZMA_PosStateBits;
extern unsigned LZMA_LiteralPosStateBits;
extern unsigned LZMA_LiteralContextBits;

/* decompress LZMA-compressed data. */
const std::vector<unsigned char> LZMADeCompress
    (const std::vector<unsigned char>& buf);

const std::vector<unsigned char> LZMADeCompress
    (const std::vector<unsigned char>& buf, bool& ok);

/* LZMA-compress data with current settings. */
const std::vector<unsigned char> LZMACompress
    (const std::vector<unsigned char>& buf);

/* LZMA-compress data with given settings. */
const std::vector<unsigned char> LZMACompress
    (const std::vector<unsigned char>& buf,
     unsigned pb,
     unsigned lp,
     unsigned lc,
     unsigned dictsize);

const std::vector<unsigned char> LZMACompress
    (const std::vector<unsigned char>& buf,
     unsigned pb,
     unsigned lp,
     unsigned lc);

/* LZMA-compress data with every settings (5*5*9 times), taking the best.
 * It will consume a lot of time and output useful statistics,
 * so a context parameter ("why") is also given.
 */
const std::vector<unsigned char> LZMACompressHeavy
    (const std::vector<unsigned char>& buf,
     const char* why = "?");

const std::vector<unsigned char> LZMACompressAuto
    (const std::vector<unsigned char>& buf,
     const char* why = "?");

const std::vector<unsigned char>
    DoLZMACompress(int HeavyLevel,
        const std::vector<unsigned char>& data, const char* why = "?");

/*
LZMA compressed file format
 ---------------------------
 Offset Size Description
   0     1   Special LZMA properties for compressed data
   1     4   Dictionary size (little endian)
   5     8   Uncompressed size (little endian). -1 means unknown size
  13         Compressed data
*/
#endif
