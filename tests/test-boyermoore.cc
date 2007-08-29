#include "../lib/boyermooreneedle.hh"
#include "../lib/boyermoore.cc"
#include <cstdlib>
#include <cstdio>

static unsigned fails = 0;

unsigned aiu(const std::vector<unsigned char>& hay,
             const std::vector<unsigned char>& needle)
{
    return BoyerMooreNeedle(needle).SearchIn(hay);
}

static const std::vector<unsigned char>
    GenRandomVector(unsigned len)
{
    std::vector<unsigned char> result(len);
    
    for(unsigned a=0; a<len; ++a)
    {
        result[a] = std::rand() % 4;
    }
    
    return result;
}

static const std::vector<unsigned char>
    GenRandomVector(unsigned minlen, unsigned maxlen)
{
    unsigned len = minlen + rand() % (maxlen-minlen);
    return GenRandomVector(len);
}    

enum kinds { Full, Horspool, Turbo };
static void Test(kinds kind)
{
    /* Generate a random haystack and a random needle */
    std::vector<unsigned char> haystack = GenRandomVector(100, 10000);
    std::vector<unsigned char> needle   = GenRandomVector(10, haystack.size()-1);
    /* Select a position randomly and ensure that the needle
     * exists in that position */
    unsigned needlepos = std::rand() % (haystack.size() - needle.size());
    std::memcpy(&haystack[needlepos], &needle[0], needle.size());
    
    /* Then see what BoyerMoore comes up with */
    const BoyerMooreNeedle ding(needle);
    
    std::printf("%u in %u... \r", needle.size(), haystack.size());
    std::fflush(stdout);
    
    unsigned foundpos = 0;
    switch(kind)
    {
        case Full: foundpos = ding.SearchIn(haystack); break;
        case Turbo: foundpos = ding.SearchInTurbo(haystack); break;
        case Horspool: foundpos = ding.SearchInHorspool(haystack); break;
    }
    
    if(foundpos < needlepos)
    {
        /* If the found position is smaller than what we intended,
         * check if it's actually true
         */
       
        if(std::memcmp(&haystack[foundpos], &needle[0], needle.size()) == 0)
        {
            /* Accept this position */
            needlepos = foundpos;
        }
    }
    
    if(foundpos != needlepos)
    {
        std::fprintf(stderr, "Error: needle=%u, haystack=%u, pos=%u, claims %u\n",
            needle.size(),
            haystack.size(),
            needlepos,
            foundpos);
        std::fflush(stderr);
        ++fails;
    }
}
static const unsigned granularity = 2;

static void TestWithAppend()
{
    /* Generate a random haystack and a random needle */
    std::vector<unsigned char> haystack = GenRandomVector(100, 1500);
    const std::vector<unsigned char> needle   = GenRandomVector(20, haystack.size()-1);
    
    /* Then see what BoyerMoore comes up with */
    const BoyerMooreNeedleWithAppend ding(needle);
    std::fflush(stderr);
    for(unsigned reuse_count = 0; reuse_count < 2000; ++reuse_count)
    {
        haystack = GenRandomVector(haystack.size());
        
        /* Select a position randomly and ensure that the needle
         * exists in that position */
        const unsigned hndiff = haystack.size() - needle.size();

        unsigned needlepos = hndiff + std::rand() % (needle.size()+1);
        
    #if 1
        std::memcpy(&haystack[needlepos], &needle[0],
            std::min(needle.size(),
                     haystack.size() - needlepos) );
    #else
        haystack.resize(needlepos);
        haystack.insert(haystack.end(), needle.begin(), needle.end());

    #endif
        
        /*std::printf("%u in %u (append %u)...\r",
            needle.size(), haystack.size(),
            std::max(0l, (long)(needlepos + needle.size() - haystack.size()))
          );
        std::fflush(stdout);*/
        
        unsigned foundpos = ding.SearchInTurboWithAppend(haystack, 0, granularity);
        std::fflush(stderr);
        
        if(needlepos < haystack.size() &&
        (needlepos - hndiff) % granularity) needlepos = haystack.size();
        
        if(foundpos < needlepos)
        {
            /* If the found position is smaller than what we intended,
             * check if it's actually true
             */
            unsigned compare_size = std::min(needle.size(), haystack.size() - foundpos);
           
            if(std::memcmp(&haystack[foundpos], &needle[0], compare_size) == 0)
            {
                /* Accept this position */
                needlepos = foundpos;
            }
        }
        
        if(needlepos < haystack.size() &&
        (needlepos - hndiff) % granularity) needlepos = haystack.size();
        
        if(foundpos != needlepos)
        {
            if(foundpos < haystack.size() && (foundpos - hndiff) % granularity)
            {
                std::fprintf(stderr, "Error: It ignored granularity\n");
            }
            if(needlepos < haystack.size() && (needlepos - hndiff) % granularity)
            {
                std::fprintf(stderr, "Error: We ignored granularity\n");
            }
        
            unsigned compare_size = std::min(needle.size(), haystack.size() - needlepos);
            if(std::memcmp(&haystack[needlepos], &needle[0], compare_size) != 0)
            {
                fprintf(stderr, "Test faulty\n");
            }

            std::fprintf(stderr, "Error: needle=%u, haystack=%u, wanted=%u, claims %u\n",
                needle.size(),
                haystack.size(),
                needlepos,
                foundpos);
            std::fflush(stderr);

            compare_size = std::min(needle.size(), haystack.size() - foundpos);
            if(compare_size > 0
            && std::memcmp(&haystack[foundpos], &needle[0], compare_size) == 0)
            {
                fprintf(stderr, "- though the needle was there, too\n");
            }

            ++fails;
        }
    }
}


int main(void)
{
#if 1
    for(unsigned a=0; a<250; ++a)
    {
        std::printf("\rtest %u...%50s\r", a, ""); std::fflush(stdout);
        Test(Horspool);
    }
    if(!fails)
        std::printf("BoyerMoore Horspool tests OK\n");
    else
        std::fprintf(stderr, "BoyerMoore Horspool: %u failures\n", fails);
    fails=0;
#endif
#if 1
    for(unsigned a=0; a<125000; ++a)
    {
        std::printf("\rtest %u...%50s\r", a, ""); std::fflush(stdout);
        Test(Full);
    }
    if(!fails)
        std::printf("BoyerMoore tests OK\n");
    else
        std::fprintf(stderr, "BoyerMoore: %u failures\n", fails);
    fails=0;
#endif
#if 1
    for(unsigned a=0; a<125000; ++a)
    {
        std::printf("\rtest %u...%50s\r", a, ""); std::fflush(stdout);
        Test(Turbo);
    }
    if(!fails)
        std::printf("Turbo BoyerMoore tests OK\n");
    else
        std::fprintf(stderr, "BoyerMoore: %u failures\n", fails);
    fails=0;
#endif
#if 1
    for(unsigned a=0; a<50000; ++a)
    {
        std::printf("\rtest %u...%50s\r", a, ""); std::fflush(stdout);
        TestWithAppend();
    }
    if(!fails)
        std::printf("BoyerMoore Append tests OK\n");
    else
        std::fprintf(stderr, "BoyerMoore Append: %u failures\n", fails);
    fails=0;
#endif
}
