#ifndef HHboyermoore_needle
#define HHboyermoore_needle

#ifndef __STDC_CONSTANT_MACROS
#define __STDC_CONSTANT_MACROS /* for UINT16_C etc */
#endif

#include <map>
#include <vector>
#include <string>
#include <algorithm>
#include <cstring>

#include "autoptr"
#include "threadfun.hh"

#include "boyermoore.hh"


class BoyerMooreNeedle: public ptrable
{
public:
    typedef BoyerMooreSearch::occtable_type  occtable_type;
    typedef BoyerMooreSearch::skiptable_type skiptable_type;
public:
    explicit BoyerMooreNeedle(const std::vector<unsigned char>& n)
        : needle(&n[0]), nlen(n.size()), occ(), skip(nlen, nlen)
    {
        /* Preprocess the needle */
        InitOcc();
        InitSkip();
    }
    
    explicit BoyerMooreNeedle(const std::string& n)
        : needle( (const unsigned char*) n.data()), nlen(n.size()), occ(), skip(nlen, nlen)
    {
        /* Preprocess the needle */
        InitOcc();
        InitSkip();
    }
    
    explicit BoyerMooreNeedle(const unsigned char* n, size_t nl)
        : needle(n), nlen(nl), occ(), skip(nlen, nlen)
    {
        /* Preprocess the needle */
        InitOcc();
        InitSkip();
    }
    virtual ~BoyerMooreNeedle() { }

public:
    inline const unsigned char& operator[] (size_t index) const { return needle[index]; }
    inline const unsigned char* data() const { return needle; }
    inline size_t size() const { return nlen; }
    inline bool empty() const { return nlen == 0; }

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
        return BoyerMooreSearch::SearchIn(haystack, hlen,
            occ, skip,
            needle, nlen);
    }
    
    /* A turbo variant of above. */
    size_t SearchInTurbo(const std::vector<unsigned char>& haystack) const
    {
        return SearchInTurbo(&haystack[0], haystack.size());
    }
    size_t SearchInTurbo(const std::string& haystack, unsigned beginpos=0) const
    {
        return SearchInTurbo((const unsigned char*)&haystack[beginpos], haystack.size()-beginpos)+beginpos;
    }
    size_t SearchInTurbo(const unsigned char* haystack, const size_t hlen) const
    {
        return BoyerMooreSearch::SearchInTurbo(haystack, hlen,
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
        return BoyerMooreSearch::SearchInHorspool(haystack,hlen, occ, needle, nlen);
    }
    size_t SearchInWithAppend(const std::vector<unsigned char>& haystack,
                              const size_t minimum_overlap = 0,
                              const size_t overlap_granularity = 1) const
    {
        return SearchInWithAppend(&haystack[0], haystack.size(),
            minimum_overlap, overlap_granularity);
    }
    size_t SearchInWithAppend(const unsigned char* haystack, const size_t hlen,
                              const size_t minimum_overlap = 0,
                              const size_t overlap_granularity = 1) const
    {
        size_t result = SearchIn(haystack, hlen);
        if(result != hlen) return result;
        if(unlikely(overlap_granularity == 0)) return hlen;
        return SearchInWithAppendOnly(haystack, hlen, minimum_overlap, overlap_granularity);
    }

    size_t SearchInTurboWithAppend(const std::vector<unsigned char>& haystack,
                              const size_t minimum_overlap = 0,
                              const size_t overlap_granularity = 1) const
    {
        return SearchInTurboWithAppend(&haystack[0], haystack.size(),
            minimum_overlap, overlap_granularity);
    }
    size_t SearchInTurboWithAppend(const unsigned char* haystack, const size_t hlen,
                              const size_t minimum_overlap = 0,
                              const size_t overlap_granularity = 1) const
    {
        size_t result = SearchInTurbo(haystack, hlen);
        if(result != hlen) return result;
        if(unlikely(overlap_granularity == 0)) return hlen;
        return SearchInWithAppendOnly(haystack, hlen, minimum_overlap, overlap_granularity);
    }


    size_t SearchInWithAppendOnly(const std::vector<unsigned char>& haystack,
                                  const size_t minimum_overlap = 0,
                                  const size_t overlap_granularity = 1) const
    {
        return SearchInWithAppendOnly(&haystack[0], haystack.size(), minimum_overlap, overlap_granularity);
    }

    virtual size_t SearchInWithAppendOnly(const unsigned char* haystack, const size_t hlen,
                                          const size_t minimum_overlap = 0,
                                          const size_t overlap_granularity = 1) const
    {
        InterruptibleContext make_interruptible;
        
        if(likely(nlen))
        {
            size_t remain = std::min(hlen, nlen);
            while(remain >= minimum_overlap && remain > 0)
            {
                const size_t n_allowable_starting_positions =
                    remain - minimum_overlap;
                // ^ no +1 here, let the "regular append" be caught as the
                // default case, i.e. "return hlen".
                
                const unsigned char* searchptr = haystack + hlen - remain;
                /*fprintf(stderr, "searchptr=%u(%p), remain=%u \n",
                    searchptr-haystack, searchptr, remain);*/
                
                const unsigned char* leftptr =
                    ScanByte(searchptr, *needle, n_allowable_starting_positions, overlap_granularity);
                if(!leftptr) break;
                
                //fprintf(stderr, "got byte %u\n", leftptr-haystack);
                size_t remainhere = remain - (leftptr-searchptr);
                if(std::memcmp(leftptr+1, needle+1, remainhere-1) == 0)
                {
                    return (leftptr-haystack);
                }
                if(unlikely(remainhere < overlap_granularity)) break;
                remain = remainhere - overlap_granularity;
            }
        }
        return hlen;
    }
    
private:
    void InitOcc()
    {
        BoyerMooreSearch::InitOcc(occ, needle, nlen);
    }
    
    void InitSkip()
    {
        BoyerMooreSearch::InitSkip(skip, needle, nlen);
    }

private:
    BoyerMooreNeedle(const BoyerMooreNeedle&);
    BoyerMooreNeedle& operator= (const BoyerMooreNeedle&);


protected:
    const unsigned char* needle;
    const size_t nlen;

    occtable_type  occ;
    skiptable_type skip;
};

/* A version of boyer-moore needle that is specialized for append-searches */
/* For now, there is no specialized algorithm. */
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
                                          const size_t minimum_overlap = 0,
                                          const size_t overlap_granularity = 1) const
    {
        if(overlap_granularity >= 3
        || true
            /* Still have problems with the Horspool version.
             * Valgrind complains about an "Invalid read of size 8"
             * that I haven't been able to track down. So use
             * the parent version in any case.
             */
          )
        {
            // When granularity is 1, we are more optimal.
            // When it is 2, we are about equal.
            // When it is >= 3, we are slower, so use the parent.
            
            return BoyerMooreNeedle::SearchInWithAppendOnly(haystack, hlen, minimum_overlap, overlap_granularity);
        }
        if(unlikely(minimum_overlap > nlen)) return hlen;
        
        InterruptibleContext make_interruptible;
        
        /* For the tail part, we'll search for the first part of the needle,
         * first half of it, then half of that half, halving the amount
         * until a singlebyte needle is being searched...
         *
         * And, using the Horspool algorithm. The skip[] initialization
         * costs too much resources when the results are only being used
         * once.
         */
        size_t first_begin_pos = hlen - nlen;
        size_t really_first_begin_pos = first_begin_pos;
        
        size_t sublen = std::min((nlen+1)/2, (hlen+1)/2);

        const size_t max_begin_pos = hlen - minimum_overlap;
        
        for(; sublen > 0; sublen /= 2)
        {
            const size_t occ_entries_before = sub_occ.size();
            occtable_type& tabref = sub_occ[sublen];
            if(sub_occ.size()  != occ_entries_before) // if this table is uninitialized?
                BoyerMooreSearch::InitOcc(tabref, needle, sublen);
            
            size_t begin_pos = first_begin_pos;
            while(begin_pos < max_begin_pos)
            {
                //fprintf(stderr, "subneedle check %u from %u\n", sublen, begin_pos);
                size_t trial_pos =
                    begin_pos + BoyerMooreSearch::SearchInHorspool(
                        haystack+begin_pos, max_begin_pos-begin_pos,
                            //CHECK: should we use hlen or max_begin_pos here?
                        tabref,
                        needle,
                        sublen
                    );
                if(trial_pos >= max_begin_pos) break;
                //fprintf(stderr, "found at %u, test...\n", trial_pos);
               
             #if 1  // if granularity testing is done.
                int granu_error = ((trial_pos - really_first_begin_pos) % overlap_granularity);
                if(likely(overlap_granularity == 1)
                || granu_error == 0)
             #endif
                {
                    size_t check_pos  = trial_pos + sublen;
                    const unsigned char* needle_pos = needle + sublen;
                    size_t needle_remain = nlen - sublen;
                    
                    if(std::memcmp(haystack + check_pos,
                                   needle_pos,
                                   std::min(needle_remain, hlen - check_pos)
                                  ) == 0) return trial_pos;
                }
             
             #if 1 // if granularity testing is done.
                begin_pos = trial_pos + overlap_granularity - granu_error;
             #else
                begin_pos = trial_pos + 1;
             #endif
            }
            first_begin_pos += sublen;
        }
        return hlen;
    }


private:
    mutable std::map<size_t, occtable_type> sub_occ;
};

#endif
