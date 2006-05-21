#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Searches for a substring. */
/* Return value: index into the needle within haystack if found,
 *               an integer >= hlen if not found.
 */
/* Assumptions:
 *     nlen > 0
 *     hlen >= nlen
 *     pointers are valid
 *     needle+nlen < haystack || needle >= haystack+hlen
 *      (i.e. needle does not physically point into haystack)
 */
uint_fast32_t fast_memmem
    (const unsigned char* __restrict__ haystack, uint_fast32_t hlen,
     const unsigned char* __restrict__ needle, uint_fast32_t nlen) /*__attribute__((pure))*/;

#ifdef __cplusplus
}
#endif

