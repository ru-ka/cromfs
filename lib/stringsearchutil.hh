#ifndef bqtStringSearchUtilHH
#define bqtStringSearchUtilHH

#include <sys/types.h> // size_t

/* These are utility functions to be used in Boyer-Moore search (and others) */

/* This function compares the two strings starting at ptr1 and ptr2,
 * which are assumed to be strlen bytes long, and returns a size_t
 * indicating how many bytes were identical, counting from the _end_
 * of both strings.
 *
 * @ptr1:  The first string
 * @ptr2:  The second string
 * @strlen:
 *   Length of data in both @ptr1 and @ptr2.
 *   Access beyond this length is not allowed.
 * @maxlen:
 *   Maximum match length we're interested of.
 *   It must be that 0 <= @maxlen <= @strlen.
 */
size_t backwards_match_len_max_min(
    const unsigned char* const ptr1,
    const unsigned char* const ptr2,
    size_t strlen,
    size_t maxlen,
    size_t minlen
);

size_t backwards_match_len_max(
    const unsigned char* const ptr1,
    const unsigned char* const ptr2,
    size_t strlen,
    size_t maxlen
);

size_t backwards_match_len(
    const unsigned char* const ptr1,
    const unsigned char* const ptr2,
    size_t strlen
);

const unsigned char* ScanByte(
    const unsigned char* begin,
    const unsigned char byte,
    size_t n_bytes,
    const size_t granularity = 1);

#endif
