#define _LARGEFILE64_SOURCE

#include "../cromfs-defs.hh"
#include "../util/mkcromfs_sets.hh"

#include "sparsewrite.hh" // for is_zero_block
#include "fsballocator.hh"
#include "cromfs-blockindex.hh"
#include "threadworkengine.hh"

#include "longfileread.hh"
#include "longfilewrite.hh"

#include "cromfs-hashmap_lzo_sparse.hh"

#include "cromfs-blockifier.hh"
#include "cromfs-inodefun.hh"    // for CalcSizeInBlocks
#include "append.hh"
#include "newhash.h"
#include "assert++.hh"
#include "autodealloc.hh"

#include <algorithm>
#ifdef HAS_GCC_PARALLEL_ALGORITHMS
# include <parallel/algorithm>
#endif
#include <functional>
#include <stdexcept>
#include <list>
#include <set>

///////////////////////////////////////////////

static void DisplayProgress(
    const char* label,
    uint_fast64_t pos, uint_fast64_t max,
    uint_fast64_t bpos, uint_fast64_t bmax,
    const char* suffix = "")
{
    char Buf[4096];
    std::sprintf(Buf, "%s: %5.2f%% (%llu/%llu)%s",
        label,
        pos
        * 100.0
        / max,
        (unsigned long long)bpos,
        (unsigned long long)bmax,
        suffix ? suffix : "");
    if(DisplayBlockSelections)
        std::printf("%s\n", Buf);
    else
        std::printf("%s\r", Buf);
    /*
    std::printf("\33]2;mkcromfs: %s\7\r", Buf);
    */
    std::fflush(stdout);
}

///////////////////////////////////////////////

/*static const std::string DataDump(const unsigned char* data, uint_fast32_t size)
{
    std::string result;
    for(uint_fast32_t a=0; a<size; ++a)
    {
        char Buf[32];
        std::sprintf(Buf, "%s%02X", a?" ":"", data[a]);
        result += Buf;
    }
    char Buf[32];
    std::sprintf(Buf, " - %08X", newhash_calc(data, size));
    result += Buf;
    return result;
}*/

struct AutoIndexFinderParams
{
    // ref ok
    const mkcromfs_fblockset& fblocks;
    const cromfs_blockifier::autoindex_t& autoindex;

    bool                  auto_found;
    cromfs_block_internal which_auto_found;
    MutexType             auto_index_lock;
    MutexType             auto_which_lock;
    cromfs_blockifier::autoindex_t::find_index_t
                          auto_which_index;

    const unsigned char* data;
    uint_fast32_t        size;
    newhash_t    crc;

    AutoIndexFinderParams(
        const mkcromfs_fblockset& f,
        const cromfs_blockifier::autoindex_t& a,
        const unsigned char* d,
        uint_fast32_t        s,
        newhash_t   c)
            : fblocks(f), autoindex(a),
              auto_found(false), which_auto_found(),
              auto_index_lock(), auto_which_lock(),
              auto_which_index(),
              data(d), size(s), crc(c)
    {
    }
};

static bool FindAutoIndexNext_lock(AutoIndexFinderParams& params, cromfs_block_internal& auto_match)
{
    ScopedLock lck(params.auto_index_lock);
    bool res = params.autoindex.Find(params.crc, auto_match, params.auto_which_index);
    ++params.auto_which_index;
    return res;
}
static bool FindAutoIndexNext_unlocked(AutoIndexFinderParams& params, cromfs_block_internal& auto_match)
{
    bool res = params.autoindex.Find(params.crc, auto_match, params.auto_which_index);
    ++params.auto_which_index;
    return res;
}

static bool TestAutoIndexMatch(AutoIndexFinderParams& params, const cromfs_block_internal& auto_match)
{
    const mkcromfs_fblock& fblock = params.fblocks[auto_match.get_fblocknum(BSIZE,FSIZE)];
    const uint_fast32_t my_offs = auto_match.get_startoffs(BSIZE,FSIZE);

    /* Notice: my_offs + data_size may be larger than the fblock size.
     * This can happen if there is a collision in the checksum index. A smaller
     * block might have been indexed, and it matches to a larger request.
     * We must check for that case, and reject if it is so.
     */

    DataReadBuffer Buffer; uint_fast32_t BufSize;
  {
    ScopedLock lck(fblock.GetMutex());
    fblock.InitDataReadBuffer(Buffer, BufSize, my_offs, params.size);
  }

    if(BufSize >= my_offs + params.size
    && std::memcmp(Buffer.Buffer, &params.data[0], params.size) == 0)
    {
        if(params.auto_which_lock.TryLock())
        {
            params.auto_found       = true;
            params.which_auto_found = auto_match;
            params.auto_which_lock.Unlock();
        }
    }
    return params.auto_found;
}

const cromfs_blockifier::ReusingPlan cromfs_blockifier::CreateReusingPlan(
    const unsigned char* data, uint_fast32_t size,
    const newhash_t crc)
{
    /* Use hashing to find candidate identical blocks. */

    assertbegin();
    assert(crc == newhash_calc(data, size));
    assertflush();

    AutoIndexFinderParams params(fblocks, autoindex, data, size, crc);

#if 0
    cromfs_block_internal tmp;
    while(FindAutoIndexNext_unlocked(params, tmp))
        if(TestAutoIndexMatch(params, tmp)) break;

#else
    static ThreadWorkEngine<AutoIndexFinderParams> engine;
    engine.RunUntil(UseThreads, params,
                    FindAutoIndexNext_lock,
                    FindAutoIndexNext_unlocked,
                    TestAutoIndexMatch);
#endif

    /* Match? */
    if(params.auto_found) return ReusingPlan(crc, params.which_auto_found);

    /* Didn't find match. */
    return ReusingPlan(false);
}

struct SmallestInfo
{
    int                found; // 0=no, 1=yes, 2=full_overlap
    cromfs_fblocknum_t fblocknum;
    uint_fast32_t      adds;
    AppendInfo         appended;
    mutable MutexType    mutex;

    SmallestInfo():
        found(0), fblocknum(0), adds(0),
        appended(), mutex()
    {
    }

    int get_found() const
    {
        ScopedLock lck(mutex);
        return found;
    }
    void reset()
    {
        ScopedLock lck(mutex);
        found=0; fblocknum=0; adds=0; appended=AppendInfo();
    }
};

static void FindOverlap(
    const BoyerMooreNeedleWithAppend& data,
    const cromfs_fblocknum_t fblocknum,
    const mkcromfs_fblock& fblock,
    size_t& minimum_test_pos,
    SmallestInfo& smallest)
{
    ScopedLock lck(smallest.mutex);
    const long smallest_adds = smallest.adds;
    lck.Unlock();

    DataReadBuffer Buffer; long FblockSize; { uint_fast32_t tmpsize;
    fblock.InitDataReadBuffer(Buffer, tmpsize); FblockSize = tmpsize; }
    // Doesn't need to lock the fblock mutex here; the fblock
    // at hand here is only processed by one thread at a time.

    long max_may_add = smallest.found ? smallest_adds : data.size();
    if(FblockSize + max_may_add > FSIZE) max_may_add = FSIZE - FblockSize;
    if(max_may_add < 0) max_may_add = 0;

    long minimum_pos     = minimum_test_pos;
    long minimum_overlap = data.size() - max_may_add;
    if(minimum_overlap > FblockSize
    || minimum_pos + (long)data.size() > FblockSize + max_may_add)
    {
        // No way this is going to work, skip this fblock
        return;
    }

    if(AutoIndexPeriod == 1 && BSIZE == data.size())
    {
        // If autoindexperiod covers all possible full overlaps,
        // then we need to only search for appends.
        minimum_pos = std::max(0l, (long)(FblockSize - data.size() + 1));
    }

    AppendInfo appended = AnalyzeAppend(
        data, minimum_pos, minimum_overlap, OverlapGranularity,
        Buffer.Buffer, FblockSize);

    minimum_test_pos = appended.AppendBaseOffset;

    uint_fast32_t this_adds = appended.AppendedSize - appended.OldSize;
    uint_fast32_t overlap_size = data.size() - this_adds;

    bool this_is_good =
        overlap_size
        ? (appended.AppendedSize <= (uint_fast32_t)FSIZE)
        : (appended.AppendedSize <= (uint_fast32_t)(FSIZE - MinimumFreeSpace));

    lck.LockAgain();

    /*printf("For fblock %u(%u/(%u-%u)), minimum=%u, appendpos=%u, adds=%u, overlaps=%u, becomes=%u, good=%s\n",
        (unsigned)fblocknum,
        (unsigned)FblockSize,
        (unsigned)FSIZE, (unsigned)MinimumFreeSpace,
        (unsigned)minimum_pos,
        (unsigned)appended.AppendBaseOffset,
        (unsigned)this_adds,
        (unsigned)overlap_size,
        (unsigned)appended.AppendedSize,
        this_is_good ? "true":"false");*/

    if(!smallest.found)
        {} // ok
    else if(this_adds < smallest.adds)
        {} // ok
    else if(this_adds == smallest.adds && fblocknum < smallest.fblocknum)
        {}
    else
        this_is_good = false;

    if(this_is_good)
    {
        smallest.found     = this_adds == 0 ? 2 : 1;
        smallest.fblocknum = fblocknum;
        smallest.adds      = this_adds;
        smallest.appended  = appended;
    }
}

struct OverlapFinderParameter
{
    // ref ok
    const mkcromfs_fblockset& fblocks;
    // ref not ok
    const BoyerMooreNeedleWithAppend& data;
    // ref not ok
    cromfs_blockifier::overlaptest_history_t& minimum_tested_positions;

    // actual value:
    SmallestInfo smallest;

    // actual value:
    std::vector<cromfs_fblocknum_t> candidates;

    mutable MutexType       mutex;

    /* ICC complains of a missing constructor, but we
     * cannot use a constructor, otherwise the {} initialization
     * will be invalid syntax later on.
     */

    OverlapFinderParameter(
        const mkcromfs_fblockset& f,
        const BoyerMooreNeedleWithAppend& d,
        cromfs_blockifier::overlaptest_history_t& m)
            : fblocks(f),
              data(d),
              minimum_tested_positions(m),
              smallest(),
              candidates(),
              mutex()
    {
    }
};

static bool OverlapFindWorker(size_t a, OverlapFinderParameter& params)
{
    ScopedLock lck(params.mutex);

    const int necessarity = a < MaxFblockCountForBruteForce ? 2 : 1;

    const int found = params.smallest.get_found();
    if(found == 2) return true; // cancel all

    const bool is_necessary = necessarity > params.smallest.get_found();
    if(!is_necessary) return false;

    const cromfs_fblocknum_t fblocknum = params.candidates[a];
    size_t& minimum_tested_pos = params.minimum_tested_positions[fblocknum];

    lck.Unlock();

    const mkcromfs_fblock& fblock = params.fblocks[fblocknum];
    // Doesn't need to lock the fblock mutex here; the fblock
    // at hand here is only processed by one thread at a time.

    FindOverlap(params.data,
                fblocknum,
                fblock,
                minimum_tested_pos,
                params.smallest);

    return params.smallest.get_found() >= 2;
}

const cromfs_blockifier::WritePlan cromfs_blockifier::CreateWritePlan(
    const BoyerMooreNeedleWithAppend& data, newhash_t crc,
    overlaptest_history_t& minimum_tested_positions) const
{
#if 0
  {
    int i = fblocks.FindFblockThatHasAtleastNbytesSpace(data.size());
    if(i >= 0)
    {
        AppendInfo appended;
        appended.SetAppendPos(fblocks[i].size(), data.size());
        return WritePlan(i, appended, data, crc);
    }
    /* Our plan is then, create a new fblock just for this block! */
    AppendInfo appended;
    appended.SetAppendPos(0, data.size());
    return WritePlan(fblocks.size(), appended, data, crc);
  }
#endif

    /* First check if we can write into an existing fblock. */
    if(true)
    {
        OverlapFinderParameter params(fblocks, data, minimum_tested_positions);

        std::vector<cromfs_fblocknum_t>& candidates = params.candidates; // write here
        candidates.reserve(fblocks.size()); // prepare to consider _all_ fblocks as candidates

        /* First candidate: The fblock that we would get without brute force. */
        /* I.e. the fblock with smallest fitting hole. */
        { int i = fblocks.FindFblockThatHasAtleastNbytesSpace(data.size() + MinimumFreeSpace);
          if(i >= 0)
          {
            //std::printf("Considering fblock %d for appending %u\n", i, (unsigned)data.size());
            candidates.push_back(i);
          }
          else
          {
            //std::printf("No block with %u bytes of room\n", (unsigned)data.size());
          }
          int j = fblocks.FindFblockThatHasAtleastNbytesSpace(data.size());
          if(i != j)
            candidates.push_back(j);
        }

        /* Next candidates: last N (up to MaxFblockCountForBruteForce) */
        cromfs_fblocknum_t j = fblocks.size();
        while(j > 0 && candidates.size() < MaxFblockCountForBruteForce)
        {
            --j;
            if((candidates.size() < 1 || j != candidates[0])
            && (candidates.size() < 2 || j != candidates[1]))
                candidates.push_back(j);
        }

#if 0
        unsigned priority_candidates = candidates.size();

        /* This code would get run only when the smallest fitting hole fblock
         * has no room for a block (and there's no full overlap). On large
         * filesystems, it causes a large memory use, so it's better be disabled.
         */

        /* Add all the rest of fblocks as non-priority candidates. */
        for(cromfs_fblocknum_t a=fblocks.size(); a-- > 0; )
        {
            /*__label__ skip_candidate; - not worth using gcc extension here */
            for(unsigned b=0; b<priority_candidates; ++b)
                if(a == candidates[b]) goto skip_candidate;
            candidates.push_back(a);
          skip_candidate: ;
        }

        /* Randomly shuffle the non-priority candidates */
        MAYBE_PARALLEL_NS::
            random_shuffle(candidates.begin()+priority_candidates, candidates.end());
#endif

        /* Task description:
         * Check each candidate (up to MaxFblockCountForBruteForce)
         * for the fit which reuses the maximum amount of data.
         */

        // First ensure that all the candidates are mmapped,
        // because doing a search otherwise is stupidly unoptimal.
        for(size_t a=0; a<candidates.size(); ++a)
            fblocks[candidates[a]].EnsureMMapped();

        static ThreadWorkEngine<OverlapFinderParameter> engine;
        engine.RunTasks(UseThreads, candidates.size(),
                        params,
                        OverlapFindWorker);

        /* Utilize the finding, if it's an overlap,
         * or it's an appension into a fblock that still has
         * so much room that it doesn't conflict with MinimumFreeSpace.
         * */
        if(params.smallest.found)
        {
            const cromfs_fblocknum_t fblocknum = params.smallest.fblocknum;
            AppendInfo appended = params.smallest.appended;

            /* This is the plan. */
            return WritePlan(fblocknum, appended, data, crc);
        }

        /* Oh, so it didn't fit anywhere! */
        //std::printf("No fit? Creating fblock %u\n", (unsigned)fblocks.size());
    }

    /* Our plan is then, create a new fblock just for this block! */
    AppendInfo appended;
    appended.SetAppendPos(0, data.size());
    return WritePlan(fblocks.size(), appended, data, crc);
}

/* Execute a reusing plan */
cromfs_blocknum_t cromfs_blockifier::Execute(const ReusingPlan& plan, uint_fast32_t blocksize)
{
    const cromfs_block_internal& block = plan.block;

    const cromfs_fblocknum_t fblocknum = block.get_fblocknum(BSIZE,FSIZE);
    const uint_fast32_t startoffs      = block.get_startoffs(BSIZE,FSIZE);

    /* If this match didn't have a real block yet, create one */
    cromfs_blocknum_t blocknum = CreateNewBlock(block);
    if(DisplayBlockSelections)
    {
        std::printf("block %u => (%u) [%u @ %u] (autoindex hit) (overlap fully)\n",
            (unsigned)blocknum,
            (unsigned)blocksize,
            (unsigned)fblocknum,
            (unsigned)startoffs);
    }

    autoindex.Del(plan.crc, block); // reduce the autoindex size

    /* Autoindex the block right after this, just in case we get a match */
    PredictiveAutoIndex(fblocknum, startoffs+blocksize, blocksize);

    return blocknum;
}

void cromfs_blockifier::PredictiveAutoIndex(
    cromfs_fblocknum_t fblocknum,
    uint_fast32_t startoffs,
    uint_fast32_t blocksize)
{
    const mkcromfs_fblock& fblock = fblocks[fblocknum];

    uint_fast32_t after_block = startoffs + blocksize;

    if(after_block <= fblock.size())
    {
        DataReadBuffer Buffer; uint_fast32_t BufSize;
        fblock.InitDataReadBuffer(Buffer, BufSize, startoffs, blocksize);
        TryAutoIndex(fblocknum, Buffer.Buffer, blocksize, startoffs);
    }
    else if(fblocknum+1 < fblocks.size())
    {
        // So we could not index the next block after our match...
        // But how about the next after that?

        uint_fast32_t new_block_start = after_block - fblock.size();
        PredictiveAutoIndex(fblocknum+1, new_block_start, blocksize);
    }
}

/* Execute an appension plan */
cromfs_blocknum_t cromfs_blockifier::Execute(
    const WritePlan& plan)
{
    const cromfs_fblocknum_t fblocknum = plan.fblocknum;
    const AppendInfo& appended         = plan.appended;

    /*
        DoUpdateBlockIndex may be "false" when called
        by EvaluateBlockifyOrders() to figure out
        what happens to fblocks as part of planning.

        In normal conduct, it is "true".
    */

    /* Note: This line may automatically create a new fblock. */
    mkcromfs_fblock& fblock = fblocks[fblocknum];

#if 0
  {
    fblock.put_appended_raw(appended, &plan.data[0], plan.data.size());
    cromfs_block_internal block;
    block.define(fblocknum, appended.AppendBaseOffset);
    fblocks.UpdateFreeSpaceIndex(fblocknum, std::max(0l, (long)FSIZE - (long)appended.AppendedSize));
    fblocks.FreeSomeResources();
    return CreateNewBlock(block);
  }
#endif

    const uint_fast32_t new_data_offset = appended.AppendBaseOffset;
    const uint_fast32_t new_raw_size = appended.AppendedSize;
    const uint_fast32_t old_raw_size = appended.OldSize;

    const int_fast32_t new_remaining_room = FSIZE - new_raw_size;

    const uint_fast32_t after_block = new_data_offset + plan.data.size();

    if(DisplayBlockSelections)
    {
        if(after_block <= old_raw_size)
        {
            std::printf("block %u => (%u) [%u @ %u] (overlap fully)",
                (unsigned)blocks.size(),
                (unsigned)plan.data.size(),
                (unsigned)fblocknum,
                (unsigned)new_data_offset);
        }
        else
        {
            std::printf("block %u => (%u) [%u @ %u] size now %u, remain %d",
                (unsigned)blocks.size(),
                (unsigned)plan.data.size(),
                (unsigned)fblocknum,
                (unsigned)new_data_offset,
                (unsigned)new_raw_size,
                (int)new_remaining_room);

            if(new_data_offset < old_raw_size)
            {
                std::printf(" (overlap %d)", (int)(old_raw_size - new_data_offset));
            }
        }
        if(new_remaining_room < 0)
        {
            std::printf(" (OVERUSE)");
        }
        std::printf("\n");
    }

#if 1
    if(after_block <= old_raw_size)
    {
        /* In case of a full overlap,
         * also autoindex the block right after this, just in case we get a match */
        PredictiveAutoIndex(fblocknum, after_block, plan.data.size());
    }
#endif

    fblock.put_appended_raw(appended, &plan.data[0], plan.data.size());

/**/
#if 1
    if(true) //DoUpdateBlockIndex
    {
        static const struct different_bsizes: public std::vector<long>
        {
            different_bsizes()
            {
                #define bins(v) do { \
                    std::vector<long>::iterator vi = std::lower_bound(begin(), end(), v); \
                    if(vi == (*this).end() || *vi != v) (*this).insert(vi, v); \
                } while(0)
                bins(BSIZE);
                for(std::vector<std::pair<std::string, long> >::const_iterator
                    i = BSIZE_FOR.begin(); i != BSIZE_FOR.end(); ++i)
                {
                    bins(i->second);
                }
                #undef bins
            }
        } different_bsizes;

        for(size_t a=0; a<different_bsizes.size(); ++a)
        {
            const long bsize = different_bsizes[a];

            size_t& last_raw_size = last_autoindex_length[fblocknum][bsize];

            if(new_raw_size - last_raw_size >= 1024*256
            || new_raw_size >= FSIZE-MinimumFreeSpace - bsize)
            {
                AutoIndex(fblocknum, last_raw_size, new_raw_size, bsize);
                last_raw_size = new_raw_size;
            }
        }
    }
/**/
#endif

    cromfs_block_internal block;
    block.define(fblocknum, new_data_offset);

    fblocks.UpdateFreeSpaceIndex(fblocknum,
        new_remaining_room >= (int)MinimumFreeSpace
            ? new_remaining_room
            : 0 );

    fblocks.FreeSomeResources();

    /* Create a new blocknumber */
    return CreateNewBlock(block);
}


///////////////////////////////////////////////

/* How many automatic indexes can be done in this amount of data? */
static int CalcAutoIndexCount(int_fast32_t raw_size, uint_fast32_t bsize)
{
    if(!AutoIndexPeriod) return 0;

    int_fast32_t a = (raw_size - bsize + AutoIndexPeriod);
    return a / (int_fast32_t)AutoIndexPeriod;
}

void cromfs_blockifier::TryAutoIndex(
    const cromfs_fblocknum_t fblocknum,
    const unsigned char* ptr,
    uint_fast32_t bsize,
    uint_fast32_t startoffs)
{
    const newhash_t crc = newhash_calc(ptr, bsize);

    /* Check whether the block has already been indexed
     */
#if 1
    if(CreateReusingPlan(ptr, bsize, crc))
    {
        /* Already indexed, don't autoindex */
        return;
    }
#endif
    /* Add it to the index */
    cromfs_block_internal match;
    match.define(fblocknum, startoffs);

    // Add to the index. (We just checked, it is not already there.)
    autoindex.Add(crc, match);
}

void cromfs_blockifier::AutoIndexBetween(const cromfs_fblocknum_t fblocknum,
    const unsigned char* ptr,
    uint_fast32_t min_offset,
    uint_fast32_t max_size,
    uint_fast32_t bsize,
    uint_fast32_t stepping)
{
    while(min_offset + bsize <= max_size)
    {
        TryAutoIndex(fblocknum, ptr, bsize, min_offset);
        ptr          += stepping;
        min_offset   += stepping;
    }
}

void cromfs_blockifier::AutoIndex(const cromfs_fblocknum_t fblocknum,
    uint_fast32_t old_raw_size,
    uint_fast32_t new_raw_size,
    uint_fast32_t bsize)
{
    if(!AutoIndexPeriod) return;

    const mkcromfs_fblock& fblock = fblocks[fblocknum];

#if 0 /* NES indexing */
    if(bsize == 128)
    {
        const uint_fast32_t min_offset = old_raw_size, new_size = new_raw_size - min_offset;
        DataReadBuffer Buffer; uint_fast32_t BufSize;
        fblock.InitDataReadBuffer(Buffer, BufSize, min_offset, new_size);
        const unsigned char* const new_raw_data = Buffer.Buffer;

        static const unsigned char NES_SIG[4] = {'N','E','S',0x1A};
        static const BoyerMooreNeedle nes_needle(NES_SIG, 4);
        /* Search for NES headers */

        size_t prevpos = new_size;
        for(size_t searchpos=0; ; )
        {
            const size_t nespos = nes_needle.SearchIn(new_raw_data+searchpos, new_size-searchpos)+searchpos;
            if(prevpos < new_size)
            {
                AutoIndexBetween(fblocknum, new_raw_data+prevpos, prevpos+min_offset, new_raw_size, bsize, bsize);
                prevpos = new_size;
            }
            if(nespos+bsize > new_size) break;
            if(new_raw_data[nespos+4] < 0x10 && new_raw_data[nespos+9] == 0x00
            && new_raw_data[nespos+10] == 0x00)
            {
                prevpos = nespos;
            }
        }
    }
#endif

    /* Index all new checksum data */
    const int OldAutoIndexCount = std::max(CalcAutoIndexCount(old_raw_size,bsize),0);
    const int NewAutoIndexCount = std::max(CalcAutoIndexCount(new_raw_size,bsize),0);
    if(NewAutoIndexCount > OldAutoIndexCount && NewAutoIndexCount > 0)
    {
        const uint_fast32_t min_offset = AutoIndexPeriod * OldAutoIndexCount;

        DataReadBuffer Buffer; uint_fast32_t BufSize;
        fblock.InitDataReadBuffer(Buffer, BufSize, min_offset, new_raw_size - min_offset);
        const unsigned char* new_raw_data = Buffer.Buffer;
        AutoIndexBetween(fblocknum, new_raw_data, min_offset, new_raw_size, bsize, AutoIndexPeriod);
    }
}

///////////////////////////////////////////////

class bitset1p32
{
    /* Don't implement this in terms of lzo-map. It will completely
     * kill the performance, especially due to the number of lockings
     * needed.
     *
     * Implementing it through rangeset is also probably a bad idea,
     * considering that the access patterns are completely random.
     */
    typedef size_t r;
    enum { rbits = 8 * sizeof(r) };
    r data[UINT64_C(0x100000000) / rbits];
public:
    bitset1p32()
    {
        std::memset(&data, 0, sizeof(data));
    }
    bool  test(uint_fast32_t p) const { return data[getpos(p)] & getmask(p); }
    void   set(uint_fast32_t p)              { data[getpos(p)] |= getmask(p); }
    void unset(uint_fast32_t p)              { data[getpos(p)] &= ~getmask(p); }
private:
    r getmask(uint_fast32_t p) const { return r(1) << r(p % rbits); }
    r getpos(uint_fast32_t p)  const { return p / rbits; }
};

template<typename schedule_item, size_t max_open>
class schedule_cache
{
    std::vector<schedule_item>& schedule;
    std::list<size_t, FSBAllocator<size_t> > open_list;
    size_t n_open;
    MutexType lock;
public:
    bool can_close;
    typedef schedule_item sched_item_t;
public:
    schedule_cache(std::vector<schedule_item>& s)
        : schedule(s), open_list(), can_close(true), n_open(0), lock()
    {
    }
    ~schedule_cache()
    {
        for(std::list<size_t>::const_iterator
            i = open_list.begin(); i != open_list.end(); ++i)
                schedule[*i].GetDataSource()->close();
    }

    schedule_item& Get(size_t pos)
    {
        ScopedLock lck(lock);

        //printf("Get(%lu): %lu are open\n", (unsigned long)pos, (unsigned long)n_open);

        for(std::list<size_t>::iterator
            i = open_list.begin(); i != open_list.end(); ++i)
        {
            if(*i == pos)
            {
                if(i != open_list.begin())
                {
                    // todo: use splice or something
                    open_list.erase(i);
                    open_list.push_front(pos);
                }
                if(can_close)
                {
                    while(n_open > max_open)
                    {
                        schedule[open_list.back()].GetDataSource()->close();
                        open_list.pop_back();
                        --n_open;
                    }
                }
                return schedule[pos];
            }
        }

        if(can_close)
        {
            while(n_open >= max_open)
            {
                schedule[open_list.back()].GetDataSource()->close();
                open_list.pop_back();
                --n_open;
            }
        }

        open_list.push_front(pos);
        schedule[pos].GetDataSource()->open();
        ++n_open;

        return schedule[pos];
    }
};

class BlockWhereList
{
public:
    template<typename schedlist>
    typename schedlist::sched_item_t*
        Find(schedlist& sched, uint_fast32_t want_blockno, uint_fast64_t& filepos)
    {
        typedef std::vector<uint_least32_t>::const_iterator it;
        it i = std::lower_bound(block_list.begin(), block_list.end(), want_blockno);
        if(i != block_list.end() && *i == want_blockno)
        {
            size_t schedno = i - block_list.begin();
            filepos = 0;
            return &sched.Get(schedno);
        }
        assert(i != block_list.begin());
        --i;
        size_t schedno = i - block_list.begin();
        uint_fast32_t begin_blockno = *i;
        typename schedlist::sched_item_t* s = &sched.Get(schedno);
        uint_fast64_t blocksize = s->GetBlockSize();
        filepos = (want_blockno - begin_blockno) * blocksize;
        return s;
    }

    void Add(size_t schedno, uint_fast32_t blocknum)
    {
        assertbegin();
        assert2var(schedno, block_list.size());
        assert(schedno == block_list.size());
        assertflush();

        block_list.push_back(blocknum);
    }
private:
    std::vector<uint_least32_t/*blocknum*/> block_list/*scheduleno*/;
};

#ifndef NDEBUG
static void VerifyInodeIntact(
    const unsigned char* blockdata_begin,
    uint_fast64_t nbytes,
    uint_fast64_t blocksize,
    const std::string& name)
{
    /* target is assumed to always point to the beginning
     * of an inode's block list. Calculate the address
     * of the inode header, and check whether it's sane.
     */
    bool has_variable_blocksizes = storage_opts & CROMFS_OPT_VARIABLE_BLOCKSIZES;

    const unsigned char* inodeheader = blockdata_begin - INODE_HEADER_SIZE();
    const uint_fast32_t inomode    = R32(inodeheader+0x00);
    const uint_fast64_t inolength  = R64(inodeheader+0x10);
    const uint_fast32_t inoblksize = has_variable_blocksizes ? R32(inodeheader+0x18) : blocksize;
    const long calc_bsize = CalcBSIZEfor(name);

    const void* inodehdr = (const void*)inodeheader;
    assertbegin();
    assert8var(nbytes, blocksize, inolength, inomode, inoblksize, name, calc_bsize, inodehdr);
    assert(nbytes == inolength);
    assert(inoblksize == blocksize);
    assert(blocksize = calc_bsize);
    if(name != "INOTAB")
    {
        assert(S_ISDIR(inomode) || S_ISREG(inomode) || (S_ISLNK(inomode) && nbytes < 1000));
    }
    assertflush();
}
#endif

typedef std::pair<uint_least32_t/* current whereinfo index */,
                  uint_least32_t/* previous whereinfo index, match */
                 > identical_item;
class identical_list
{
private:
    std::vector<identical_item> data;
    uint_fast64_t fsize, dontneed_cap;
    LongFileRead* rd;
    LongFileWrite* wt;
    int fd;
public:
    identical_list() : data(), rd(0), fsize(0), dontneed_cap(0), wt(0), fd(-1) { }
    ~identical_list()
    {
        delete rd;
        delete wt;
        if(fd >= 0) close(fd);
    }

    bool Load(const std::string& fn)
    {
        if(fn.empty()) return false;

        fd = open(fn.c_str(), O_RDONLY | O_LARGEFILE);
        if(fd >= 0)
        {
            fsize = lseek64(fd, 0, SEEK_END);
            rd = new LongFileRead(fd, 0, fsize);
            MadviseSequential(rd->GetAddr(), fsize);
            std::printf("Identical_list loaded from %s\n", fn.c_str());
            return true;
        }
        return false;
    }

    void BeginSaving(const std::string& fn)
    {
        if(!rd)
        {
            fsize = 0;
            if(!fn.empty())
            {
                std::printf("Identical_list will be saved to %s\n", fn.c_str());
                fd = open(fn.c_str(), O_RDWR | O_CREAT | O_TRUNC | O_LARGEFILE, 0644);
                wt = new LongFileWrite(fd, 0);
            }
        }
    }

    void EndSaving()
    {
        if(wt) { delete wt; wt = 0; }
        if(fd >= 0)
        {
            fsync(fd);
            fdatasync(fd);
            if(rd) { delete rd; rd = 0; }
        }
    }

    void BeginAccessing()
    {
        if(fd >= 0)
        {
            if(!rd) rd = new LongFileRead(fd, 0, fsize);
            MadviseSequential(rd->GetAddr(), fsize);
            close(fd);
            fd = -1;
        }
        else
            MadviseSequential(&data[0], data.size() * (uint_fast64_t)sizeof(identical_item));

        dontneed_cap = 0;
    }

    size_t size() const { if(fd >= 0) return fsize / sizeof(identical_item); else return data.size(); }

    const identical_item& operator[] (size_t index) const
    {
        if(rd)
            return *(const identical_item*) (rd->GetAddr() + index * (uint_fast64_t)sizeof(identical_item));
        return data[index];
    }

    void Append(const identical_item& item)
    {
        if(wt)
            wt->write( (const unsigned char*)&item, sizeof(item), fsize, false);
        else
            data.push_back(item);
        fsize += sizeof(item);
    }

    void DoneWithUntil(size_t index)
    {
        uint_fast64_t pos = index * (uint_fast64_t)sizeof(identical_item);
        pos &= ~4095;

        if(pos <= dontneed_cap) return;
        dontneed_cap = pos;

        if(rd)
            MadviseDontNeed(rd->GetAddr(), pos);
        else
            MadviseDontNeed(&data[0], pos);
    }
private:
    identical_list(const identical_list&);
    void operator=(const identical_list&);
};

void cromfs_blockifier::FlushBlockifyRequests(const char* purpose)
{
    // Note: using __gnu_parallel in this sort() will crash the program.
    // Probably because of autoptr not being threadsafe.
    std::stable_sort(schedule.begin(), schedule.end(),
       std::mem_fun_ref(&schedule_item::CompareSchedulingOrder) );

    uint_fast64_t total_size = 0, blocks_total = 0;
    for(size_t a=0; a<schedule.size(); ++a)
    {
        uint_fast64_t size = schedule[a].GetDataSource()->size();
        long blocksize     = schedule[a].GetBlockSize();
        total_size   += size;
        blocks_total += CalcSizeInBlocks(size, blocksize);
    }

    bitset1p32* hash_duplicate = 0;
    autodealloc<bitset1p32> hash_dupl_dealloc(hash_duplicate);

    identical_list identical_list;

    std::string ReuseListFileName;
    if(!ReuseListFile.empty())
        ReuseListFileName = ReuseListFile + "[" + purpose + "].bin";

    bool reuselist_already_loaded = identical_list.Load(ReuseListFileName);
    if(!reuselist_already_loaded && BlockHashing_Method != BlockHashing_None)
    {
        identical_list.BeginSaving(ReuseListFileName);
    }

    if(BlockHashing_Method == BlockHashing_All_Prepass
    && !reuselist_already_loaded)
    {
        static const char label[] = "Finding identical hashes";

        std::printf("Beginning task for %s: %s\n", purpose, label);

        // Precollect list of duplicate hashes
        bitset1p32* hash_seen = new bitset1p32;
        autodealloc<bitset1p32> hash_seen_dealloc(hash_seen);
        hash_duplicate = new bitset1p32;

        uint_fast64_t total_done  = 0;
        uint_fast64_t blocks_done = 0;
        uint_fast64_t last_report_pos = 0;
        uint_fast64_t n_collisions = 0;
        uint_fast64_t n_unique     = 0;

        enum { n_hashlocks = 4096 };
        MutexType hashlock[n_hashlocks+1], displaylock;

        DataReadBuffer buf;
        #pragma omp parallel for schedule(guided) private(buf) shared(n_unique,n_collisions)
        for(size_t a=0; a < schedule.size(); ++a)
        {
            schedule_item& s = schedule[a];
            datasource_t* source  = s.GetDataSource();
            //unsigned char* target = s.GetBlockTarget();
            uint_fast64_t nbytes  = source->size();
            uint_fast64_t blocksize = s.GetBlockSize();

            if(DisplayBlockSelections)
            {
                std::printf("%s <size %llu, block size %llu>\n",
                    source->getname().c_str(),
                    (unsigned long long)nbytes,
                    (unsigned long long)blocksize);
            }

            source->open();
            for(uint_fast64_t offset=0; offset<nbytes; offset += blocksize)
            {
                if(total_done - last_report_pos >= 1048576*4) // at 4 MB intervals
                {
                    if(displaylock.TryLock())
                    {
                        DisplayProgress(label, total_done, total_size, blocks_done, blocks_total);
                        displaylock.Unlock();
                        last_report_pos = total_done;
                    }
                }

                uint_fast64_t eat = blocksize;
                if(offset+eat > nbytes) eat = nbytes-offset;

                source->read(buf, eat, offset);
                const newhash_t hash = newhash_calc(buf.Buffer, eat);

                hashlock[hash / (UINT64_C(0x100000000) / n_hashlocks)].Lock();
                if(hash_seen->test(hash))
                {
                    if(!hash_duplicate->test(hash))
                    {
                        hash_duplicate->set(hash);
                        #pragma omp atomic
                        ++n_collisions;
                    }
                }
                else
                {
                    hash_seen->set(hash);
                    #pragma omp atomic
                    ++n_unique;
                }
                hashlock[hash / (UINT64_C(0x100000000) / n_hashlocks)].Unlock();

              #pragma omp atomic
                total_done += eat;
              #pragma omp atomic
                ++blocks_done;
            }
            source->close();
        }
        DisplayProgress(label, total_done, total_size, blocks_done, blocks_total);
        std::printf("%lu recurring hashes found. %lu unique. Prepass helped skip about %.1f%% of work.\n",
            (unsigned long) n_collisions,
            (unsigned long) n_unique,
            n_unique
              ? (n_unique - n_collisions) * 100.0 / n_unique
              : 0.0
                   );

        //delete hash_seen;
        //hash_seen = 0;
        // ^autodealloc deals with this
    }

    BlockWhereList where_list;
    if(true)
    {
        schedule_cache<schedule_item, 10> schedule_cache(schedule);
        const char* const label =
            (BlockHashing_Method != BlockHashing_None && !reuselist_already_loaded)
            ? "Finding identical blocks"
            : "Mapping the block list";

        std::printf("Beginning task for %s: %s\n", purpose, label);

        uint_fast64_t total_done = 0, blocks_done  = 0;
        uint_fast64_t last_report_pos = 0;

        typedef block_index_stack
            <newhash_t, uint_least32_t,
             CompressedHashLayer_Sparse<newhash_t, uint_least32_t>
            > blocks_list;
        blocks_list blockhashlist;

        for(size_t a=0; a < schedule.size(); ++a)
        {
            where_list.Add(a, blocks_done);

            const schedule_item& s = schedule_cache.Get(a);
            datasource_t* source  = s.GetDataSource();
            //unsigned char* target = s.GetBlockTarget();
            uint_fast64_t nbytes  = source->size();
            uint_fast64_t blocksize = s.GetBlockSize();

            if(DisplayBlockSelections)
            {
                std::printf("%s <size %llu, block size %llu>\n",
                    source->getname().c_str(),
                    (unsigned long long)nbytes,
                    (unsigned long long)blocksize);
            }

            if(BlockHashing_Method == BlockHashing_None || reuselist_already_loaded)
            {
                blocks_done += CalcSizeInBlocks(nbytes, blocksize);
                continue;
            }


            DataReadBuffer buf;
            for(uint_fast64_t offset=0; offset<nbytes; offset += blocksize)
            {
                if(total_done - last_report_pos >= 1048576*4) // at 4 MB intervals
                {
                    //if(displaylock.TryLock())
                    //{
                        DisplayProgress(label, total_done, total_size, blocks_done, blocks_total);
                        //displaylock.Unlock();
                        last_report_pos = total_done;
                    //}
                }

                uint_fast64_t eat = blocksize;
                if(offset+eat > nbytes) eat = nbytes-offset;

                /*
                std::printf("Eating %llu bytes @ %llu",
                    (unsigned long long) eat,
                    (unsigned long long) offset);
                */
                source->read(buf, eat, offset);
                newhash_t hash = 0;
                /*
                std::printf("... hash %08X\n", (unsigned) hash);
                */

                bool do_checkhash = false;

                switch(BlockHashing_Method)
                {
                    case BlockHashing_None:
                        break;
                    case BlockHashing_BlanksOnly:
                        if(!is_zero_block(buf.Buffer, eat))
                            do_checkhash = false;
                        else
                            hash = newhash_calc(buf.Buffer, eat);
                        break;
                    case BlockHashing_All_Prepass:
                        hash = newhash_calc(buf.Buffer, eat);
                        do_checkhash = hash_duplicate->test(hash);
                        break;
                    case BlockHashing_All:
                        hash = newhash_calc(buf.Buffer, eat);
                        do_checkhash = true;
                        break;
                }
                if(do_checkhash) // Check the hash contents?
                {
                    schedule_cache.can_close = false;
                    // ^ Ensure that the handle to source, and s,
                    //   will not surprisingly become invalid in
                    //   the identical_found loop that follows.

                    size_t         index = 0;
                    MutexType      index_lock;
                    bool           identical_found = false;
                    MutexType      identical_lock;

                    /*
                      The identical block selection is configurable.
                         Options:
                          all:
                              Always run identical block check (default)
                          blanks:
                              Only run it for blocks consisting entirely
                              of some particular byte value
                          none:
                              Never run it (fastest, no block-merging benefits whatsoever)
                          prepass:
                              Run a pre-pass to find out which hashes might overlap
                              at all. Maintain two bitmasks:
                                bitmask_1: This hash has been sighted.
                                bitmask_2: This hash was already sighted when it was
                                           sighted again, so it must be an overlap.
                             In the actual pass, add to the hashmap only those hashes
                             that were marked in bitmask_2.
                    */

                  DataReadBuffer buf2;
                #pragma omp parallel private(buf2)
                  {
                    for(;;)
                    {
                        #pragma omp flush(identical_found)
                        if(identical_found) break;

                        uint_least32_t other;
                        #pragma omp flush(index)
                        { ScopedLock lck(index_lock);
                          #pragma omp flush(identical_found)
                          if(identical_found) break;
                          // ^Check again because acquiring the lock could have taken time
                          if(!blockhashlist.Find(hash, other, index)) break;
                          ++index;
                          #pragma omp flush(index)
                        }

                        uint_fast64_t filepos;
                        schedule_item* s2 = where_list.Find(schedule_cache, other, filepos);
                        datasource_t* source2  = s2->GetDataSource();
                        uint_fast64_t nbytes2  = source2->size();
                        uint_fast64_t blocksize2 = s2->GetBlockSize();

                        assertbegin();
                        assert2var(other, blocks_done);
                        assert(other < blocks_done);
                        assertflush();

                        if(blocksize > blocksize2) continue;
                        if(filepos + eat > nbytes2) continue;

                        source2->read(buf2, eat, filepos);

                        if(!std::memcmp(buf.Buffer, buf2.Buffer, eat))
                        {
                            // It is an exact match.
                            if(identical_lock.TryLock())
                            {
                                //std::printf("... matches block %llu\n", (unsigned long long)other);
                                #pragma omp flush(identical_found)
                                if(!identical_found)
                                {
                                    identical_list.Append( identical_item(blocks_done, other) );
                                    identical_found = true;
                                    #pragma omp flush(identical_found)
                                }
                                identical_lock.Unlock();
                            }
                            break;
                        }
                    }
                  } // end of scope for omp parallel

                    if(!identical_found)
                    {
                        // Add to the blockhashlist. It cannot already be there,
                        // because this is the first time that this particular
                        // value of blocks_done is handled.
                        blockhashlist.Add(hash, blocks_done);
                        // ^ This is not thread-safe, so don't run it in a thread context.
                    }

                    schedule_cache.can_close = true;
                    schedule_item& s2 = schedule_cache.Get(a); // ensure it's not unloaded
                } // check hash contents

                /*
                where_all[blocks_done].first  = a;
                where_all[blocks_done].second = offset;
                */

                //target += BLOCKNUM_SIZE_BYTES(); // where block number will be written to.
                total_done += eat;
                ++blocks_done;
            }
        }

        if(!reuselist_already_loaded)
            identical_list.EndSaving();

        DisplayProgress(label, total_done, total_size, blocks_done, blocks_total);
        std::printf("%lu reuse opportunities found. %lu unique blocks. Block table will be %.1f%% smaller than without the index search.\n",
            (unsigned long) identical_list.size(),
            (unsigned long) (blocks_done - identical_list.size()),
            100.0 - (blocks_done - identical_list.size()) * 100.0 / blocks_done
                   );
        std::fflush(stdout);

        blocks.reserve(blocks.size() + blocks_done - identical_list.size());

        delete hash_duplicate;
        hash_duplicate = 0;
    }

    if(true)
    {
        schedule_cache<schedule_item, 10> schedule_cache(schedule);
        static const char label[] = "Blockifying";

        std::printf("Beginning task for %s: %s\n", purpose, label);

        uint_fast64_t total_done=0, blocks_done=0;
        uint_fast64_t last_report_pos = 0;

        identical_list.BeginAccessing();

        size_t identical_list_pos=0;

        DataReadBuffer buf;
        for(size_t a=0; a < schedule.size(); ++a)
        {
            schedule_item& s = schedule_cache.Get(a);
            datasource_t* source  = s.GetDataSource();
            unsigned char* target = s.GetBlockTarget();
            uint_fast64_t nbytes  = source->size();
            uint_fast64_t blocksize = s.GetBlockSize();
            unsigned char* target_end =
                target + BLOCKNUM_SIZE_BYTES() * ((nbytes + blocksize-1) / blocksize);

            if(DisplayBlockSelections)
            {
                std::printf("%s <size %llu, block size %llu>\n",
                    source->getname().c_str(),
                    (unsigned long long)nbytes,
                    (unsigned long long)blocksize);
            }
        #ifndef NDEBUG
            VerifyInodeIntact(target, nbytes, blocksize, source->getname());
        #endif

            //printf("rangebegin %p\nrange  end %p...\n", target, target_end);
            for(uint_fast64_t offset=0; offset<nbytes; offset += blocksize)
            {
                assertbegin();
                const void* tgt_next = (const void*)(target + BLOCKNUM_SIZE_BYTES());
                const void* tgt_end  = (const void*)target_end;
                assert2var(tgt_next, tgt_end);
                assert(tgt_next <= tgt_end);
                assertflush();

                if(DisplayBlockSelections
                || total_done - last_report_pos >= 1048576*4) // at 4 MB intervals
                {
                    //if(displaylock.TryLock())
                    //{
                        DisplayProgress(label, total_done, total_size, blocks_done, blocks_total,
                                        (" autoindex("+autoindex.GetStatistics()+")").c_str());
                        //displaylock.Unlock();
                        last_report_pos = total_done;
                    //}
                }
                uint_fast64_t eat = blocksize;
                if(offset+eat > nbytes) eat = nbytes-offset;

                while(identical_list_pos < identical_list.size()
                   && identical_list[identical_list_pos].first < blocks_done)
                {
                    ++identical_list_pos;
                }
                if(identical_list_pos < identical_list.size()
                && identical_list[identical_list_pos].first == blocks_done)
                {
                    size_t other = identical_list[identical_list_pos].second;
                    // Make us simply a reference to that block's
                    // blocknumber, whatever it might have been.

                    // Find the schedule item
                    uint_fast64_t filepos;
                    schedule_item* s2 = where_list.Find(schedule_cache, other, filepos);

                    datasource_t* source2  = s2->GetDataSource();
                    unsigned char* target2 = s2->GetBlockTarget();
                    uint_fast64_t blocksize2 = s2->GetBlockSize();
        #ifndef NDEBUG
                    VerifyInodeIntact(target2, source2->size(), blocksize2, source2->getname());
        #endif
                    // Find the blocknumber
                    uint_fast64_t blockindex2 = filepos / blocksize2;
                    uint_fast64_t blocknum = Rn(target2 + blockindex2 * BLOCKNUM_SIZE_BYTES(), BLOCKNUM_SIZE_BYTES());
                    // Write the blocknumber here
                    //printf("writing to %p (%u)...\n", target, BLOCKNUM_SIZE_BYTES());
                    Wn(target, blocknum, BLOCKNUM_SIZE_BYTES());
                #if 0
                    assertbegin();
                    assert8var(blocknum, blocks_done,other, blocks.size(), filepos, blocksize2, blocksize, blockindex2);
                    assert(other    < blocks_done);
                    assert(blocknum < blocks.size());
                    assertflush();
                #endif

                    if(DisplayBlockSelections)
                    {
                        const cromfs_block_internal& block = blocks[blocknum];

                        const cromfs_fblocknum_t fblocknum = block.get_fblocknum(BSIZE,FSIZE);
                        const uint_fast32_t startoffs      = block.get_startoffs(BSIZE,FSIZE);

                        std::printf("block %u == (%u) [%u @ %u] (reused block)\n",
                            (unsigned)blocknum,
                            (unsigned)blocksize,
                            (unsigned)fblocknum,
                            (unsigned)startoffs);
                    }
                }
                else
                {
                    source->read(buf, eat, offset);
                    // Decide the placement within fblocks.
                    // PLAN: 1. Find from autoindex...
                    //       2. Find from a selection of fblocks
                    //       3. Append
                    // In any case, a new block number is created.

                    const newhash_t hash = newhash_calc(buf.Buffer, eat);
                    if(ReusingPlan reuse = CreateReusingPlan(buf.Buffer, eat, hash))
                    {
                        cromfs_blocknum_t blocknum = Execute(reuse, eat);
                        //printf("writing to %p (%u)...\n", target, BLOCKNUM_SIZE_BYTES());
                        Wn(target, blocknum, BLOCKNUM_SIZE_BYTES());
                    }
                    else
                    {
                        overlaptest_history_t hist;
                        BoyerMooreNeedleWithAppend needle(buf.Buffer, eat);
                        WritePlan write = CreateWritePlan(needle, hash, hist);

                        cromfs_blocknum_t blocknum = Execute(write);

                        //printf("writing to %p (%u)...\n", target, BLOCKNUM_SIZE_BYTES());
                        Wn(target, blocknum, BLOCKNUM_SIZE_BYTES());
                    }
                }
                target += BLOCKNUM_SIZE_BYTES(); // where block number will be written to.
                total_done += eat;
                ++blocks_done;
            }

            identical_list.DoneWithUntil(identical_list_pos);
        }
    }
    schedule.clear();
}

void cromfs_blockifier::ScheduleBlockify(
    datasource_t* source,
    SchedulerDataClass dataclass,
    unsigned char* blocknum_target,
    long BlockSize)
{
    schedule.push_back(schedule_item(source, dataclass, blocknum_target, BlockSize));
}

void cromfs_blockifier::NoMoreBlockifying()
{
    //block_index.clear();
}

///////////////////////////////////////////////

void cromfs_blockifier::EnablePackedBlocksIfPossible()
{
    /*long MinimumBsize = BSIZE;
    for(std::vector<std::pair<std::string, long> >::const_iterator
        i = BSIZE_FOR.begin(); i != BSIZE_FOR.end(); ++i)
    {
        MinimumBsize = std::min(MinimumBsize, i->second);
    }*/
    long MinimumBsize = 1; // partial blocks may be even this small.

    if(MayPackBlocks)
    {
        uint_fast64_t max_blockoffset = FSIZE - MinimumBsize;
        max_blockoffset *= fblocks.size();
        if(max_blockoffset < UINT64_C(0x100000000))
        {
            storage_opts |= CROMFS_OPT_PACKED_BLOCKS;
            std::printf(
                "mkcromfs: Automatically enabling --packedblocks because it is possible for this filesystem.\n");
        }
    }

    /* Only check the possibility of -2, -3 and -4.
     * Do not change anything, because doing so will
     * invalidate all already encoded inodes.
     */
    if(blocks.size() < 0x10000UL)
    {
        if(!(storage_opts & CROMFS_OPT_16BIT_BLOCKNUMS))
        {
            std::printf( "mkcromfs: --16bitblocknums would have been possible for this filesystem. But you didn't select it.\n");
            if(MayAutochooseBlocknumSize)
                std::printf("          (mkcromfs did not automatically do this, because the block merging went better than estimated.)\n");
        }
    }
    else if(blocks.size() < 0x1000000UL)
    {
        if(!(storage_opts & CROMFS_OPT_24BIT_BLOCKNUMS))
        {
            if(storage_opts & CROMFS_OPT_16BIT_BLOCKNUMS)
            {
                std::printf( "mkcromfs: You used --16bitblocknums. However, only --24bitblocknums was possible for this filesystem. Your filesystem is likely corrupt.\n");
            }
            else
            {
                std::printf( "mkcromfs: --24bitblocknums would have been possible for this filesystem. But you didn't select it.\n");
                if(MayAutochooseBlocknumSize)
                    std::printf("          (mkcromfs did not automatically do this, because the block merging went better than estimated.)\n");
            }
        }
    }
    else if(storage_opts & CROMFS_OPT_16BIT_BLOCKNUMS)
    {
        std::printf( "mkcromfs: You used --16bitblocknums. However, only --32bitblocknums was possible for this filesystem. Your filesystem is likely corrupt.\n");
    }
    else if(storage_opts & CROMFS_OPT_24BIT_BLOCKNUMS)
    {
        std::printf( "mkcromfs: You used --24bitblocknums. However, only --32bitblocknums was possible for this filesystem. Your filesystem is likely corrupt.\n");
    }
}

#include <sstream>
const std::string cromfs_blockifier::autoindex_t::GetStatistics() const
{
    std::stringstream out;
    out << "size=" << added << ",del=" << deleted;
    return out.str();
}
