#include <fnmatch.h>
#include <fstream>

#include "fnmatch.hh"

bool MatchFile(const std::string& pathname, const std::string& pattern)
{
    int result;
    //#pragma omp critical(fnmatch) // fnmatch() is not re-entrant?
    result = fnmatch(
        pattern.c_str(),
        pathname.c_str(),
        0
#if 0
        | FNM_PATHNAME
        /* disabled. It is nice if *Zelda* also matches subdir/Zelda. */
#endif
#ifdef FNM_LEADING_DIR
        | FNM_LEADING_DIR
        /* GNU extension which does exactly what I want --Bisqwit
         * With this, one can enter pathnames to the commandline and
         * those will too be extracted, without need to append / and *
         */
#endif
      );
    return result == 0;
}

bool MatchFileFrom(const std::string& pathname, MatchingFileListType& list, bool empty_means)
{
    if(list.empty()) return empty_means;

    bool retval = false;
    for(unsigned a=0; a<list.size(); ++a)
    {
        const std::string& pattern = list[a].first;

        if(MatchFile(pathname, pattern))
        {
            retval = true;
            /* Don't return immediately. Otherwise, we won't catch it
             * if multiple patterns matched (for reporting about unmatched
             * patterns)
             */
            list[a].second = true;
        }
    }
    return retval;
}

void AddFilePattern(MatchingFileListType& list, const std::string& pattern)
{
    list.push_back(std::make_pair(pattern, false));
}

void AddFilePatternsFrom(MatchingFileListType& list, const std::string& filename)
{
    std::ifstream f(filename.c_str());
    while(f.good())
    {
        std::string line;
        std::getline(f, line);
        AddFilePattern(list, line);
    }
}

const UnmatchedPatternListType GetUnmatchedList(const MatchingFileListType& list)
{
    UnmatchedPatternListType result;
    for(unsigned a=0; a<list.size(); ++a)
        if(!list[a].second)
            result.push_back(list[a].first);
    return result;
}
