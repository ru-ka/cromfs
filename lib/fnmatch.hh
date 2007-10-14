#include <string>
#include <vector>

typedef std::vector< std::pair<std::string, bool> > MatchingFileListType;

bool MatchFile(const std::string& pathname, const std::string& pattern);
bool MatchFileFrom(const std::string& pathname, MatchingFileListType& list, bool empty_means=true);

void AddFilePattern(MatchingFileListType& list, const std::string& pattern);
void AddFilePatternsFrom(MatchingFileListType& list, const std::string& filename);

typedef std::vector<std::string> UnmatchedPatternListType;

const UnmatchedPatternListType GetUnmatchedList(const MatchingFileListType& list);
