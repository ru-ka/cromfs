#ifndef bqtBoyerMooreUtilHH
#define bqtBoyerMooreUtilHH

#include <stdint.h>
#include <vector>

#include <limits.h> // UCHAR_MAX

#ifndef UCHAR_MAX
#define UCHAR_MAX ((unsigned char)~0u)
#endif

namespace BoyerMooreSearch
{
    struct occtable_type
    {
        size_t data[UCHAR_MAX+1];
        inline size_t& operator[] (size_t pos) { return data[pos]; }
        inline size_t operator[] (size_t pos) const { return data[pos]; }
    };
    typedef std::vector<size_t> skiptable_type;

    /////////////////////

    occtable_type
        InitOcc(const unsigned char* needle, const size_t needle_length);

    skiptable_type
         InitSkip(const unsigned char* needle, const size_t needle_length);

    void InitOcc(occtable_type& occ, const unsigned char* needle, const size_t needle_length);

    /* Note: this function expects skip to be resized to needle_length and initialized with needle_length. */
    void InitSkip(skiptable_type& skip, const unsigned char* needle, const size_t needle_length);

    //////////////////

    size_t SearchInHorspool(const unsigned char* haystack, const size_t haystack_length,
        const occtable_type& occ,
        const unsigned char* needle, const size_t needle_length);

    size_t SearchIn(const unsigned char* haystack, const size_t haystack_length,
        const occtable_type& occ,
        const skiptable_type& skip,
        const unsigned char* needle, const size_t needle_length);

    size_t SearchInTurbo(const unsigned char* haystack, const size_t haystack_length,
        const occtable_type& occ,
        const skiptable_type& skip,
        const unsigned char* needle, const size_t needle_length);
}

#endif
