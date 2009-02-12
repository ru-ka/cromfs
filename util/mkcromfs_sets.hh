#ifndef bqtMkCromfsSetsHH
#define bqtMkCromfsSetsHH

#include <vector>
#include <string>
#include <utility>

extern int LZMA_HeavyCompress;
extern bool DecompressWhenLookup;
extern bool MayAutochooseBlocknumSize;
extern bool MayPackBlocks;
extern bool DisplayBlockSelections;
extern unsigned RandomCompressPeriod;
extern uint_fast32_t MinimumFreeSpace;
extern uint_fast32_t AutoIndexPeriod;
extern uint_fast32_t MaxFblockCountForBruteForce;
extern unsigned UseThreads;
extern uint_fast32_t OverlapGranularity;

extern long FSIZE;
extern long BSIZE;
extern std::vector<std::pair<std::string, long> > BSIZE_FOR;

//extern uint_fast32_t MaxSearchLength;

/* Order in which to blockify different types of data */
typedef char SchedulerDataClass;

extern uint_fast32_t storage_opts;

extern const char* GetTempDir();

enum BlockHashingMethods
    { BlockHashing_All,
      BlockHashing_All_Prepass,
      BlockHashing_BlanksOnly,
      BlockHashing_None
    };
extern BlockHashingMethods BlockHashing_Method;
extern std::string ReuseListFile;

long CalcBSIZEfor(const std::string& pathfn); // from mkcromfs.cc

#endif
