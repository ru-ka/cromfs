#ifndef HHboyermoore_needle
#define HHboyermoore_needle

#include <stdint.h>

#include <map>
#include <vector>
#include <string>
#include <algorithm>
#include <string.h>

#include "autoptr"
#include "threadfun.hh"

#ifdef __GNUC__
# define likely(x)       __builtin_expect(!!(x), 1)
# define unlikely(x)     __builtin_expect(!!(x), 0)
#else
# define likely(x)   (x)
# define unlikely(x) (x)
#endif

class BoyerMooreNeedle: public ptrable
{
public:
    explicit BoyerMooreNeedle(const std::vector<unsigned char>& n)
        : needle(&n[0]), nlen(n.size()), skip(nlen, nlen)
    {
        /* Preprocess the needle */
        InitOcc();
        InitSkip();
    }
    
    explicit BoyerMooreNeedle(const std::string& n)
        : needle( (const unsigned char*) n.data()), nlen(n.size()), skip(nlen, nlen)
    {
        /* Preprocess the needle */
        InitOcc();
        InitSkip();
    }
    
    explicit BoyerMooreNeedle(const unsigned char* n, size_t nl)
        : needle(n), nlen(nl), skip(nlen, nlen)
    {
        /* Preprocess the needle */
        InitOcc();
        InitSkip();
    }
    virtual ~BoyerMooreNeedle() { }

protected:
    struct IgnoreSkipTag { };
    struct IgnoreBothTag { };
    explicit BoyerMooreNeedle(const unsigned char* n, size_t nl, IgnoreSkipTag)
        : needle(n), nlen(nl), skip()
    {
        /* Preprocess the needle */
        InitOcc();
        // don't build the skip table.
    }
    explicit BoyerMooreNeedle(const unsigned char* n, size_t nl, IgnoreBothTag)
        : needle(n), nlen(nl), skip()
    {
    }

public:
    inline const unsigned char& operator[] (size_t index) const { return needle[index]; }
    inline const unsigned char* data() const { return needle; }
    inline size_t size() const { return nlen; }
    inline bool empty() const { return nlen == 0; }

public:
    struct occtable_type
    {
        ssize_t data[UCHAR_MAX+1];
        inline ssize_t& operator[] (size_t pos) { return data[pos]; }
        inline const ssize_t operator[] (size_t pos) const { return data[pos]; }
    };
    typedef std::vector<size_t> skiptable_type;

    static void InitOcc(occtable_type& occ, const unsigned char* needle, size_t nlen)
    {
        for(unsigned a=0; a<UCHAR_MAX+1; ++a) occ[a] = -1;
        
        /* Populate it with the analysis of the needle */
        /* But ignoring the last letter */
        for(size_t a=0; a+1<nlen; ++a) occ[needle[a]] = (ssize_t)a;
    }

    /* Note: this function expects skip to be resized to nlen and initialized with nlen. */
    static void InitSkip(skiptable_type& skip, const unsigned char* needle, size_t nlen)
    {
        if(unlikely(nlen <= 1)) return;
        
        /* I have absolutely no idea how this works. I just copypasted
         * it from http://www-igm.univ-mlv.fr/~lecroq/string/node14.html
         * and have since edited it in trying to seek clarity. -Bisqwit
         */
        
        std::vector<ssize_t> suff(nlen);
        
        suff[nlen-1] = nlen;
        
        for (ssize_t begin=0, end=nlen-1, i=end; i-- > 0; )
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
        
        size_t j=0;
        for (ssize_t i = nlen-1; i >= -1; --i)
            if (i == -1 || suff[i] == i + 1)
                for (; j < nlen-1 - i; ++j)
                    if (skip[j] == nlen)
                        skip[j] = nlen-1 - i;
        
        for (size_t i = 0; i < nlen-1; ++i)
            skip[nlen-1 - suff[i]] = nlen-1 - i;
    }
    
    static const size_t SearchInHorspool(const unsigned char* haystack, const size_t hlen,
        const occtable_type& occ,
        const unsigned char* needle,
        const size_t nlen)
    {
        if(unlikely(nlen > hlen)) return hlen;
        if(unlikely(nlen == 1))
        {
            const unsigned char* result = (const unsigned char*)std::memchr(haystack, *needle, hlen);
            return result ? result-haystack : hlen;
        }
        
        size_t hpos=0;
        while(hpos <= hlen-nlen)
        {
            //fprintf(stderr, "hlen=%u nlen=%u\n", hpos, nlen);
            size_t npos=nlen-1;
            while(needle[npos] == haystack[hpos+npos])
            {
                if(unlikely(npos-- == 0)) return hpos;
            }
            unsigned char occ_char = haystack[hpos+npos];
            hpos += std::max((ssize_t)(nlen - npos),
                             (ssize_t)(npos - occ[occ_char]));
        }
        return hlen;
    }

    static const size_t SearchIn(const unsigned char* haystack, const size_t hlen,
        const occtable_type& occ,
        const skiptable_type& skip,
        const unsigned char* needle,
        const size_t nlen)
    {
        if(unlikely(nlen > hlen)) return hlen;

        InterruptableContext make_interruptable;
        
        if(unlikely(nlen == 1))
        {
            const unsigned char* result = (const unsigned char*)std::memchr(haystack, *needle, hlen);
            return result ? result-haystack : hlen;
        }
        
        size_t hpos=0;
        while(hpos <= hlen-nlen)
        {
            //fprintf(stderr, "hlen=%u nlen=%u\n", hpos, nlen);
            size_t npos=nlen-1;
            while(needle[npos] == haystack[hpos+npos])
            {
                if(unlikely(npos-- == 0))
                {
                    return hpos;
                }
            }
            unsigned char occ_char = haystack[hpos+npos];
            hpos += std::max((ssize_t)skip[npos],
                             (ssize_t)(npos - occ[occ_char]));
        }
        return hlen;
    }

public:
    /* A general purpose search method. */

    size_t SearchIn(const std::vector<unsigned char>& haystack) const
    {
        return SearchIn(&haystack[0], haystack.size());
    }
    size_t SearchIn(const std::string& haystack, unsigned beginpos=0) const
    {
        return SearchIn((const unsigned char*)&haystack[beginpos], haystack.size()-beginpos)+beginpos;
    }
    size_t SearchIn(const unsigned char* haystack, const size_t hlen) const
    {
        return SearchIn(haystack, hlen,
            occ, skip,
            needle, nlen);
    }
    
    /* A variant of the general purpose search method,
     * that does not utilize the skip[] table. It only
     * uses the occ[] table. This may be used when a faster
     * startup is needed, such as in SearchInWithAppendOnly().
     */
    size_t SearchInHorspool(const std::vector<unsigned char>& haystack) const
    {
        return SearchInHorspool(&haystack[0], haystack.size());
    }
    
    size_t SearchInHorspool(const unsigned char* haystack, const size_t hlen) const
    {
        return SearchInHorspool(haystack,hlen, occ, needle, nlen);
    }
    size_t SearchInWithAppend(const std::vector<unsigned char>& haystack,
                              const size_t minimum_overlap = 0) const
    {
        return SearchInWithAppend(&haystack[0], haystack.size(), minimum_overlap);
    }
    size_t SearchInWithAppend(const unsigned char* haystack, const size_t hlen,
                              const size_t minimum_overlap = 0) const
    {
        size_t result = SearchIn(haystack, hlen);
        if(result != hlen) return result;
        
        return SearchInWithAppendOnly(haystack, hlen, minimum_overlap);
    }

    size_t SearchInWithAppendOnly(const std::vector<unsigned char>& haystack,
                                  const size_t minimum_overlap = 0) const
    {
        return SearchInWithAppendOnly(&haystack[0], haystack.size(), minimum_overlap);
    }
    virtual size_t SearchInWithAppendOnly(const unsigned char* haystack, const size_t hlen,
                                          const size_t minimum_overlap = 0) const
    {
        InterruptableContext make_interruptable;
        
        /* Disappointingly, this is faster after all -- reason being
         * that creating the occ[] table requires scanning through
         * the requested part of the needle, which is basically the
         * same as what memchr() does here. Except that memchr() is
         * optimized.
         */
        if(likely(nlen))
        {
            size_t remain = std::min(hlen, nlen);
            while(remain > minimum_overlap)
            {
                const unsigned char* searchptr = haystack + hlen - remain;
                const unsigned char* leftptr = (const unsigned char*)
                    std::memchr(searchptr, *needle, remain-minimum_overlap);
                if(!leftptr) break;
                size_t remainhere = haystack+hlen - leftptr;
                if(std::memcmp(leftptr+1, needle+1, remainhere-1) == 0)
                {
                    return (leftptr-haystack);
                }
                remain = remainhere-1;
            }
        }
        return hlen;
    }
    
private:
    void InitOcc()
    {
        InitOcc(occ, needle, nlen);
    }
    
    void InitSkip()
    {
        InitSkip(skip, needle, nlen);
    }
    
protected:
    const unsigned char* needle;
    const size_t nlen;

    occtable_type  occ;
    skiptable_type skip;
};

/* A version of boyer-moore needle that is specialized for append-searches */
class BoyerMooreNeedleWithAppend: public BoyerMooreNeedle
{
public:
    explicit BoyerMooreNeedleWithAppend(const std::vector<unsigned char>& n)
        : BoyerMooreNeedle(n), sub_occ() { }

    explicit BoyerMooreNeedleWithAppend(const std::string& n)
        : BoyerMooreNeedle(n), sub_occ() { }

    explicit BoyerMooreNeedleWithAppend(const unsigned char* n, size_t nl)
        : BoyerMooreNeedle(n, nl), sub_occ() { }

    virtual ~BoyerMooreNeedleWithAppend() { }

    virtual size_t SearchInWithAppendOnly(const unsigned char* haystack, const size_t hlen,
                                          const size_t minimum_overlap = 0) const
    {
        return BoyerMooreNeedle::SearchInWithAppendOnly(haystack, hlen, minimum_overlap);
        
        InterruptableContext make_interruptable;
        
        /* FIXME: Implement minimum_overlap */
        
        // Note: This is still buggy. It doesn't pass all test cases.
        // Therefore, the above line uses the version that _works_.
        
        /* For the tail part, we'll search for the first part of the needle,
         * first half of it, then half of that half, halving the amount
         * until a singlebyte needle is being searched...
         *
         * And, using the Horspool algorithm. The skip[] initialization
         * costs too much resources when the results are only being used
         * once.
         */
        size_t first_begin_pos = hlen - nlen;
        
        size_t sublen = std::min((nlen+1)/2, (hlen+1)/2);
        
        for(; sublen > 0; sublen /= 2)
        {
            const size_t occ_entries_before = sub_occ.size();
            occtable_type& tabref = sub_occ[sublen];
            if(sub_occ.size()  != occ_entries_before) // if this table is uninitialized?
                InitOcc(tabref, needle, sublen);
            
            size_t begin_pos = first_begin_pos;
            while(begin_pos < hlen)
            {
                //fprintf(stderr, "subneedle check %u from %u\n", sublen, begin_pos);
                size_t trial_pos =
                    begin_pos + SearchInHorspool(
                        haystack+begin_pos, hlen-begin_pos,
                        tabref,
                        needle,
                        sublen
                    );
                if(trial_pos >= hlen) break;
                //fprintf(stderr, "found at %u, test...\n", trial_pos);
                
                size_t check_pos  = trial_pos + sublen;
                const unsigned char* needle_pos = needle + sublen;
                size_t needle_remain = nlen - sublen;
                
                if(std::memcmp(haystack + check_pos,
                               needle_pos,
                               std::min(needle_remain, hlen - check_pos)
                              ) == 0) return trial_pos;
                begin_pos = trial_pos + 1;
            }
            first_begin_pos += sublen;
        }
        return hlen;
    }
private:
    mutable std::map<size_t, occtable_type> sub_occ;
};

#endif
