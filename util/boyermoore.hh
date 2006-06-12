#ifndef HHboyermoore_needle
#define HHboyermoore_needle

#include <stdint.h>

#include <vector>
#include <algorithm>
#include <string.h>

#ifdef __GNUC__
# define likely(x)       __builtin_expect(!!(x), 1)
# define unlikely(x)     __builtin_expect(!!(x), 0)
#else
# define likely(x)   (x)
# define unlikely(x) (x)
#endif

class BoyerMooreNeedle
{
public:
    explicit BoyerMooreNeedle(const std::vector<unsigned char>& n)
        : needle(&n[0]), nlen(n.size()), skip(nlen, nlen)
    {
        /* Preprocess the needle */
        InitOcc();
        InitSkip();
    }
    
    explicit BoyerMooreNeedle(const unsigned char* n, uint_fast32_t nl)
        : needle(n), nlen(nl), skip(nlen, nlen)
    {
        /* Preprocess the needle */
        InitOcc();
        InitSkip();
    }
    
public:
    inline const unsigned char& operator[] (uint_fast32_t index) const { return needle[index]; }
    inline uint_fast32_t size() const { return nlen; }
    inline bool empty() const { return nlen == 0; }
    
public:
    uint_fast32_t SearchIn(const std::vector<unsigned char>& haystack) const
    {
        return SearchIn(&haystack[0], haystack.size());
    }
    uint_fast32_t SearchIn(const unsigned char* haystack, const uint_fast32_t hlen) const
    {
        if(unlikely(nlen == 1))
        {
            const unsigned char* result = (const unsigned char*)memchr(haystack, *needle, hlen);
            return result ? result-haystack : hlen;
        }
        if(unlikely(nlen > hlen)) return hlen;
        
        uint_fast32_t hpos=0;
        while(hpos <= hlen-nlen)
        {
            //fprintf(stderr, "hlen=%u nlen=%u\n", hpos, nlen);
            uint_fast32_t npos=nlen-1;
            while(needle[npos] == haystack[npos+hpos])
            {
                if(unlikely(npos-- == 0)) return hpos;
            }
            hpos += std::max((int_fast32_t)skip[npos],
                             (int_fast32_t)(npos - occ[haystack[npos+hpos]]));
        }
        return hlen;
    }
    
private:
    void InitOcc()
    {
        for(unsigned a=0; a<UCHAR_MAX+1; ++a) occ[a] = -1;
        
        /* Populate it with the analysis of the needle */
        /* But ignoring the last letter */
        for(uint_fast32_t a=0; a<nlen-1; ++a) occ[needle[a]] = (int_fast32_t)a;
    }
    void InitSkip()
    {
        if(unlikely(nlen <= 1)) return;
        
        /* I have absolutely no idea how this works. I just copypasted
         * it from http://www-igm.univ-mlv.fr/~lecroq/string/node14.html
         * and have since edited it in trying to seek clarity. -Bisqwit
         */
        
        int_fast32_t suff[nlen];
        
        suff[nlen-1] = nlen;
        
        for (int_fast32_t begin=0, end=nlen-1, i=end; i-- > 0; )
        {
            if (i > end && suff[i + nlen-1 - begin] < i - end)
                suff[i] = suff[i + nlen-1 - begin];
            else
            {
                begin = i;
                if(i < end) end = i;
                while (end >= 0 && needle[end] == needle[end + nlen-1 - begin])
                    --end;
                suff[i] = begin - end;
                
                /* "end" may go signed in this while loop. ^ */
                /* "i" is unsigned. */
                /* "begin" only contains values of "i", hence it is unsigned. */
                /* All are made "signed" here to avoid comparisons between
                 * signed and unsigned values.
                 */
            }
        }
        
        uint_fast32_t j=0;
        for (int_fast32_t i = nlen-1; i >= -1; --i)
            if (i == -1 || suff[i] == i + 1)
                for (; j < nlen-1 - i; ++j)
                    if (skip[j] == nlen)
                        skip[j] = nlen-1 - i;
        
        for (uint_fast32_t i = 0; i < nlen-1; ++i)
            skip[nlen-1 - suff[i]] = nlen-1 - i;
    }
private:
    const unsigned char* needle;
    const uint_fast32_t nlen;

    int_fast32_t occ[UCHAR_MAX+1];
    std::vector<uint_fast32_t> skip;
};

#endif
