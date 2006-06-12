#include "../util/boyermoore.hh"
#include <cstdlib>
#include <cstdio>

static unsigned fails = 0;

unsigned aiu(const std::vector<unsigned char>& hay,
             const std::vector<unsigned char>& needle)
{
    return BoyerMooreNeedle(needle).SearchIn(hay);
}

static const std::vector<unsigned char>
    GenRandomVector(unsigned minlen, unsigned maxlen)
{
    unsigned len = minlen + rand() % (maxlen-minlen);
    
    std::vector<unsigned char> result(len);
    
    for(unsigned a=0; a<len; ++a)
    {
        result[a] = std::rand() & 255;
    }
    return result;
}

static void Test()
{
    /* Generate a random haystack and a random needle */
    std::vector<unsigned char> haystack = GenRandomVector(100, 1000000);
    std::vector<unsigned char> needle   = GenRandomVector(40, haystack.size()-1);
    /* Select a position randomly and ensure that the needle
     * exists in that position */
    unsigned needlepos = std::rand() % (haystack.size() - needle.size());
    std::memcpy(&haystack[needlepos], &needle[0], needle.size());
    
    /* Then see what BoyerMoore comes up with */
    const BoyerMooreNeedle ding(needle);
    
    std::printf("%u in %u... \r", needle.size(), haystack.size());
    std::fflush(stdout);
    
    unsigned foundpos = ding.SearchIn(haystack);
    
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


int main(void)
{
    for(unsigned a=0; a<250; ++a)
    {
        std::printf("\rtest %u...%50s\r", a, ""); std::fflush(stdout);
        Test();
    }
    
    if(!fails)
        std::printf("BoyerMoore tests OK\n");
    else
        std::fprintf(stderr, "BoyerMoore: %u failures\n", fails);
}
