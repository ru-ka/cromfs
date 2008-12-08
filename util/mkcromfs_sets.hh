#ifndef bqtMkCromfsSetsHH
#define bqtMkCromfsSetsHH

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

// Number of blockifys to keep in buffer, hoping for optimal sorting
extern size_t BlockifyAmount1;
// Number of blockifys to add to buffer at once
extern size_t BlockifyAmount2;
// 0 = nope. 1 = yep, 2 = use TSP, 3 = yes, and try combinations too
extern int TryOptimalOrganization;

extern const char* GetTempDir();

#endif
