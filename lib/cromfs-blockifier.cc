#define _LARGEFILE64_SOURCE

#include "../cromfs-defs.hh"
#include "../util/mkcromfs_sets.hh"

#include "cromfs-blockifier.hh"
#include "cromfs-inodefun.hh"    // for CalcSizeInBlocks
#include "superstringfinder.hh"
#include "threadworkengine.hh"
#include "append.hh"

#include <algorithm>
#ifdef HAS_GCC_PARALLEL_ALGORITHMS
# include <parallel/algorithm>
#endif
#include <functional>
#include <stdexcept>
#include <set>

///////////////////////////////////////////////

static void DisplayProgress(
    const char* label,
    uint_fast64_t pos, uint_fast64_t max,
    uint_fast64_t bpos, uint_fast64_t bmax)
{
    char Buf[4096];
    std::sprintf(Buf, "%s: %5.2f%% (%llu/%llu)",
        label,
        pos
        * 100.0
        / max,
        (unsigned long long)bpos,
        (unsigned long long)bmax);
    if(DisplayBlockSelections)
        std::printf("%s\n", Buf);
    else
        std::printf("%s\r", Buf);
    /*
    std::printf("\33]2;mkcromfs: %s\7\r", Buf);
    std::fflush(stdout);
    */
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
    std::sprintf(Buf, " - %08X", BlockIndexHashCalc(data, size));
    result += Buf;
    return result;
}*/

const cromfs_blockifier::ReusingPlan cromfs_blockifier::CreateReusingPlan(
    const unsigned char* data, uint_fast32_t size,
    const BlockIndexHashType crc,
    bool PreferRealIndex)
{
    /* Use hashing to find candidate identical blocks. */

    /*
    Information on threadability:
    The functions called in this section are:
      block_index.FindRealIndex()
        - This uses pread64(), does no changes to data structures
      block_index.FindAutoIndex()
        - This uses pread64(), does no changes to data structures
      block_is()
        - This accesses fblocks:
            fblocks[]
            fblock.InitDataReadBuffer()
            std::memcmp()
          For safety, that is put into a critical block.
    */
 //{ RemapReadonly ro_marker;

    /*if(crc != BlockIndexHashCalc(data, size))
    {
        throw "Wrong hash";
    }*/

#if !defined(_OPENMP) || (_OPENMP >= 200805)
    /* OPENMP 3.0 VERSION and NO OPENMP version */
    /* Goals: Search for a realindex match. If not found, search for an autoindex match. */
    /* The same code is also used for No-OPENMP case.
     * In that case, the #pragma constructs are simply ignored. */
    bool real_found = false;
    bool auto_found = false;
    cromfs_blocknum_t     which_real_found; MutexType RealWhichLock;
    cromfs_block_internal which_auto_found; MutexType AutoWhichLock;
    #pragma omp parallel default(shared)
    {
      #pragma omp single
      {
        cromfs_blocknum_t real_match;
        for(size_t matchno=0;
               !real_found
            && (!auto_found || PreferRealIndex)
            && block_index.FindRealIndex(crc, real_match, matchno);
            ++matchno)
        {
          #pragma omp task shared(real_found,which_real_found,RealWhichLock) \
                           firstprivate(real_match) shared(data,size)
          {
            const cromfs_block_internal& block = blocks[real_match];
            if(block_is(block, data, size) && RealWhichLock.TryLock())
            {
              real_found = true;
              which_real_found = real_match;
              RealWhichLock.Unlock();
            }
          }
        }
        cromfs_block_internal auto_match;
        for(size_t matchno=0;
               !auto_found
            && !real_found
            && block_index.FindAutoIndex(crc, auto_match, matchno);
            ++matchno)
        {
          #pragma omp task shared(auto_found,which_auto_found,AutoWhichLock) \
                           firstprivate(auto_match) shared(data,size)
          {
            if(block_is(auto_match, data, size) && AutoWhichLock.TryLock())
            {
              auto_found = true;
              which_auto_found = auto_match;
              AutoWhichLock.Unlock();
            }
          }
        }
      }
    }
    /* Match? */
    if(real_found) return ReusingPlan(crc, which_real_found);
    if(auto_found) return ReusingPlan(crc, which_auto_found);
#else
    /* OPENMP 2.5 VERSION */
    /* Goals: The same functionality as above. */

    cromfs_blocknum_t     which_real_found;
    cromfs_block_internal which_auto_found;
    bool real_done=false, real_found=false;
    bool auto_done=false, auto_found=false;
    size_t real_matchno=0; MutexType real_matchno_lock, real_which_lock;
    size_t auto_matchno=0; MutexType auto_matchno_lock, auto_which_lock;

    #pragma omp parallel default(shared)
    {
        for(;;) // RealIndex tests
        {
            // RealIndex testing end conditions:
            //  - No more RealIndex slots to test
            //  - Found RealIndex solution
            //  - Found AutoIndex solution (only if PreferRealIndex == false)
            #pragma omp flush(real_done,real_found,auto_found)
            if(real_done || real_found) break;
            if(auto_found && !PreferRealIndex) break;

            real_matchno_lock.Lock();
            size_t matchno;
            #pragma omp flush(real_matchno)
            matchno = real_matchno++;
            #pragma omp flush(real_matchno)
            real_matchno_lock.Unlock();

            cromfs_blocknum_t real_match;
            if(!block_index.FindRealIndex(crc, real_match, matchno))
            {
                real_done = true;
                #pragma omp flush(real_done)
                break;
            }

            const cromfs_block_internal& block = blocks[real_match];
            bool match = block_is(block, data, size);
            //printf("                            fblock %u:%u%s %s\n", (unsigned)block.fblocknum, (unsigned)block.startoffs, match ? " (match)" : " (no match)", DataDump(data, size).c_str());
            if(match)
            {
                if(real_which_lock.TryLock())
                {
                    which_real_found=real_match;
                    real_found=true;
                    #pragma omp flush(real_found,which_real_found)
                    real_which_lock.Unlock();
                }
                break;
            }
        }

        for(;;) // AutoIndex tests
        {
            // AutoIndex testing end conditions:
            //  - No more AutoIndex slots to test
            //  - Found RealIndex solution
            //  - Found AutoIndex solution
            #pragma omp flush(auto_done,real_found,auto_found)
            if(real_found || auto_found || auto_done) break;

            auto_matchno_lock.Lock();
            size_t matchno;
            #pragma omp flush(auto_matchno)
            matchno = auto_matchno++;
            #pragma omp flush(auto_matchno)
            auto_matchno_lock.Unlock();

            cromfs_block_internal auto_match;
            if(!block_index.FindAutoIndex(crc, auto_match, matchno))
            {
                auto_done = true;
                #pragma omp flush(auto_done)
                break;
            }

            const mkcromfs_fblock& fblock = fblocks[auto_match.get_fblocknum(BSIZE,FSIZE)];
            if(block_is(auto_match, data, size))
            {
                if(auto_which_lock.TryLock())
                {
                    which_auto_found=auto_match;
                    auto_found=true;
                    #pragma omp flush(auto_found,which_auto_found)
                    auto_which_lock.Unlock();
                }
                break;
            }
        }
    }

    /* Match? */
    if(real_found) return ReusingPlan(crc, which_real_found);
    if(auto_found) return ReusingPlan(crc, which_auto_found);
#endif

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
    const BoyerMooreNeedleWithAppend& data, BlockIndexHashType crc,
    overlaptest_history_t& minimum_tested_positions) const
{
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
    cromfs_blocknum_t blocknum = plan.blocknum;
    if(blocknum != NO_BLOCK)
    {
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
        return blocknum;
    }

    const cromfs_block_internal& block = plan.block;

    const cromfs_fblocknum_t fblocknum = block.get_fblocknum(BSIZE,FSIZE);
    const uint_fast32_t startoffs      = block.get_startoffs(BSIZE,FSIZE);

    /* If this match didn't have a real block yet, create one */
    blocknum = CreateNewBlock(block);
    if(DisplayBlockSelections)
    {
        std::printf("block %u => (%u) [%u @ %u] (autoindex hit) (overlap fully)\n",
            (unsigned)blocknum,
            (unsigned)blocksize,
            (unsigned)fblocknum,
            (unsigned)startoffs);
    }

    /* Assign a real blocknumber to the autoindex */
    block_index.DelAutoIndex(plan.crc, block);
    block_index.AddRealIndex(plan.crc, blocknum);

    /* Also autoindex the block right after this, just in case we get a match */
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
    const WritePlan& plan, bool DoUpdateBlockIndex)
{
    const cromfs_fblocknum_t fblocknum = plan.fblocknum;
    const AppendInfo& appended         = plan.appended;

    /* Note: This line may automatically create a new fblock. */
    mkcromfs_fblock& fblock = fblocks[fblocknum];

    const uint_fast32_t new_data_offset = appended.AppendBaseOffset;
    const uint_fast32_t new_raw_size = appended.AppendedSize;
    const uint_fast32_t old_raw_size = appended.OldSize;

    const int_fast32_t new_remaining_room = FSIZE - new_raw_size;

    if(DisplayBlockSelections)
    {
        uint_fast32_t after_block = new_data_offset + plan.data.size();

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

        if(after_block <= old_raw_size)
        {
            /* In case of a full overlap,
             * also autoindex the block right after this, just in case we get a match */
            PredictiveAutoIndex(fblocknum, after_block, plan.data.size());
        }
    }

    fblock.put_appended_raw(appended, plan.data);

    if(DoUpdateBlockIndex)
    {
        if(last_autoindex_length.size() <= fblocknum)
            last_autoindex_length.resize(fblocknum+1);

        std::set<long> different_bsizes;
        different_bsizes.insert(BSIZE);
        for(std::vector<std::pair<std::string, long> >::const_iterator
            i = BSIZE_FOR.begin(); i != BSIZE_FOR.end(); ++i)
        {
            different_bsizes.insert(i->second);
        }

        for(std::set<long>::const_iterator
            i = different_bsizes.begin(); i != different_bsizes.end(); ++i)
        {
            const long bsize = *i;

            size_t& last_raw_size = last_autoindex_length[fblocknum][bsize];

            if(new_raw_size - last_raw_size >= 1024*256
            || new_raw_size >= FSIZE-MinimumFreeSpace - bsize)
            {
                AutoIndex(fblocknum, last_raw_size, new_raw_size, bsize);
                last_raw_size = new_raw_size;
            }
        }
    }

    cromfs_block_internal block;
    block.define(fblocknum, new_data_offset);

    /* If the block is uncompressed, preserve it fblock_index
     * so that CompressOneRandomly() may pick it some day.
     *
     * Otherwise, store it in the index only if it is still a candidate
     * for crunching more bytes into it.
     */
    fblocks.UpdateFreeSpaceIndex(fblocknum,
        new_remaining_room >= (int)MinimumFreeSpace
            ? new_remaining_room
            : 0 );

    fblocks.FreeSomeResources();

    /* Create a new blocknumber and add to block index */
    const cromfs_blocknum_t blocknum = CreateNewBlock(block);
    if(DoUpdateBlockIndex)
    {
        if(!CreateReusingPlan(&plan.data[0], plan.data.size(), plan.crc, false))
        {
            // Only add to index if we don't have any existing reusing method for this one
            block_index.AddRealIndex(plan.crc, blocknum);
        }
    }
    return blocknum;
}

///////////////////////////////////////////////

bool cromfs_blockifier::block_is(
    const cromfs_block_internal& block,
    const unsigned char* data,
    uint_fast32_t data_size) const
{
    const mkcromfs_fblock& fblock = fblocks[block.get_fblocknum(BSIZE,FSIZE)];
    const uint_fast32_t my_offs = block.get_startoffs(BSIZE,FSIZE);

    ScopedLock lck(fblock.GetMutex());

    /* Notice: my_offs + data_size may be larger than the fblock size.
     * This can happen if there is a collision in the checksum index. A smaller
     * block might have been indexed, and it matches to a larger request.
     * We must check for that case, and reject if it is so.
     */

    DataReadBuffer Buffer; uint_fast32_t BufSize;
    fblock.InitDataReadBuffer(Buffer, BufSize, my_offs, data_size);

    lck.Unlock();

    return BufSize >= my_offs + data_size
          && std::memcmp(Buffer.Buffer, &data[0], data_size) == 0;
}

/* How many automatic indexes can be done in this amount of data? */
static int CalcAutoIndexCount(int_fast32_t raw_size, uint_fast32_t bsize)
{
    int_fast32_t a = (raw_size - bsize + AutoIndexPeriod);
    return a / (int_fast32_t)AutoIndexPeriod;
}

void cromfs_blockifier::TryAutoIndex(
    const cromfs_fblocknum_t fblocknum,
    const unsigned char* ptr,
    uint_fast32_t bsize,
    uint_fast32_t startoffs)
{
    const BlockIndexHashType crc = BlockIndexHashCalc(ptr, bsize);

    /* Check whether the block has already been indexed
     * (don't care whether we get a RealIndex or AutoIndex result,
     *  just see if it's indexed at all)
     */
    if(CreateReusingPlan(ptr, bsize, crc, false))
    {
        /* Already indexed */
        return;
    }
    /* Add it to the index */
    cromfs_block_internal match;
    match.define(fblocknum, startoffs);
    block_index.AddAutoIndex(crc, match);
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

void cromfs_blockifier::AddOrder(
    orderlist_t& blockify_orders,
    const std::vector<unsigned char>& data,
    uint_fast64_t offset,
    unsigned char* target)
{
    individual_order order(data, target);

    order.badness = offset; // a dummy sorting rule at first

    std::fflush(stdout); std::fflush(stderr);
    const ReusingPlan plan1 = CreateReusingPlan(order.data, order.crc);
    if(plan1)
    {
        order.Write(Execute(plan1, data.size()));
        return; // Finished, don't need to postpone it.
    }

    //printf("Adding order crc %08X\n", order.crc);
    blockify_orders.push_back(order);
}

void cromfs_blockifier::HandleOrders(
    orderlist_t& blockify_orders, ssize_t max_remaining_orders)
{
    /*
    std::printf("Handle: ");
    for(unsigned c=0,a=0, b=blockify_orders.size(); a<b; ++a)
    {
        std::printf("[%u]%08X ",
            a, blockify_orders[a].crc);
        if(++c==7 && a+1<b){printf("\n        ");c=0; }
    }
    std::printf("\n");
    */

    ssize_t num_handle = blockify_orders.size() - max_remaining_orders;
    //printf("handling %d, got %u\n", (int)num_handle, (unsigned)blockify_orders.size());

    if(num_handle <= 0) return;

    if(TryOptimalOrganization)
    {
ReEvaluate:
        EvaluateBlockifyOrders(blockify_orders);
    }
    // Now the least bad are handled first.
    blockify_orders.sort( std::mem_fun_ref(&individual_order::CompareOrder) );

    /* number of orders might have changed, recheck it */
    num_handle = blockify_orders.size() - max_remaining_orders;

    /* Don't parallelize this loop; it will do bad things
     * for compression.
     */
    for(orderlist_t::iterator j,i = blockify_orders.begin();
        num_handle > 0 && i != blockify_orders.end();
        i = j, --num_handle)
    {
        j = i; ++j;
        individual_order& order = *i;

        std::fflush(stdout); std::fflush(stderr);

        // Find the fblock that contains this given data, or if that's
        // not possible, find out which fblock to append to, or whether
        // to create a new fblock.
        const ReusingPlan plan1
            = CreateReusingPlan(order.data, order.crc);
        if(plan1)
        {
            /*
            std::printf("Plan: %u,crc %08X,badness(%g) - ",
                0,
                order.crc,
                order.badness);
            */
            order.Write(Execute(plan1, order.data.size()));
        }
        else
        {
            if(!order.needle) order.MakeNeedle();

            const WritePlan plan2 = CreateWritePlan(*order.needle, order.crc,
                order.minimum_tested_positions);
            /*
            std::printf("Plan: %u,crc %08X,badness(%g),fblock(%u)old(%u)base(%u)size(%u) - ",
                0,
                order.crc,
                order.badness,
                (unsigned)plan2.fblocknum,
                (unsigned)plan2.appended.OldSize,
                (unsigned)plan2.appended.AppendBaseOffset,
                (unsigned)plan2.appended.AppendedSize);
            */
            order.Write(Execute(plan2));
        }

        blockify_orders.erase(i);

        if(TryOptimalOrganization >= 2) goto ReEvaluate;
    }
}

void cromfs_blockifier::EvaluateBlockifyOrders(orderlist_t& blockify_orders)
{
    SuperStringFinder<orderlist_t::iterator> supfinder;

    /*
        TODO: Devise an algorithm to sort the blocks in an order that yields
        best results.

        The algorithms presented here seem to
        actually yield a *worse* compression!...
     */

    /* Algorithm 1:
     *   Sort them in optimalness order. Most optimal first.
     *   Optimalness = - (size_added / original_size)
     */
    for(orderlist_t::iterator
        j,i = blockify_orders.begin();
        i != blockify_orders.end();
        i = j)
    {
        j = i; ++j;
        individual_order& order = *i;

        const ReusingPlan plan1
            = CreateReusingPlan(order.data, order.crc);
        if(plan1)
        {
            /* Surprise, it matched. Handle it immediately. */
            order.Write(Execute(plan1, order.data.size()));
            blockify_orders.erase(i);
            continue;
        }

        /* Check how well this order would be placed */
        if(!order.needle) order.MakeNeedle();
        const WritePlan plan2 = CreateWritePlan(*order.needle, order.crc,
            order.minimum_tested_positions);
        const AppendInfo& appended = plan2.appended;
        uint_fast32_t size_added   = appended.AppendedSize - appended.OldSize;
        uint_fast32_t overlap_size = order.data.size() - size_added;
        if(!size_added)
        {
            /* It's a full overlap. Handle it immediately. */
            order.Write(Execute(plan2));
            blockify_orders.erase(i);
            continue;
        }

        order.badness = -overlap_size;

        if(TryOptimalOrganization >= 2)
        {
            supfinder.AddData(order.needle, i);
        }
    }

    /* Algorithm 2:
     *   Use the superstring finder (which reduces into asymmetric TSP)
     */
    if(TryOptimalOrganization >= 2)
    {
        std::vector<orderlist_t::iterator> sup_result;
        supfinder.Organize(sup_result);
        for(size_t a=0; a<sup_result.size(); ++a)
            sup_result[a]->badness = a;
    }

    /* Algorithm 3:
     *   Sort them in optimalness order. Most optimal first.
     *   Optimalness = - (total size if this block goes first,
     *                    and all of the remaining blocks go next )
     */
    if(TryOptimalOrganization >= 3)
    {
        /* FIXME: minimum_tested_positions does not work properly in
         *        conjunction with this algorithm.
         */
        /* A backup of data so that we can simulate an Execute(WritePlan) */
        struct SituationBackup
        {
            size_t n_blocks;
            mkcromfs_fblockset::undo_t fblock_state;

            SituationBackup(): n_blocks(),fblock_state() { }
        };

        for(orderlist_t::iterator
            j = blockify_orders.begin();
            j != blockify_orders.end();
            ++j)
        {
            const WritePlan plan = CreateWritePlan(*j->needle, j->crc,
                j->minimum_tested_positions);

            uint_fast32_t size_added_here
                = plan.appended.AppendedSize - plan.appended.OldSize;

            // create backup
            SituationBackup backup;
            backup.n_blocks     = blocks.size();
            backup.fblock_state = fblocks.create_backup();

            // try what happens
            std::printf("> false write- ");
            Execute(plan, false);

            uint_fast64_t size_added_sub = 0, size_added_count = 0;
            for(orderlist_t::iterator
                i = blockify_orders.begin();
                i != blockify_orders.end();
                ++i)
            {
                if(i == j) continue;
                const WritePlan plan2 =
                    CreateWritePlan(*i->needle, i->crc,
                        i->minimum_tested_positions);
                const AppendInfo& appended = plan2.appended;
                size_added_sub += appended.AppendedSize - appended.OldSize;
                ++size_added_count;
            }

            j->badness = size_added_here;
            if(size_added_count)
                j->badness += size_added_sub / (double)size_added_count;

            std::printf(">> badness %g\n", j->badness);

            // restore backup
            blocks.resize(backup.n_blocks);
            fblocks.restore_backup(backup.fblock_state);
        }
    }
}


///////////////////////////////////////////////

void cromfs_blockifier::FlushBlockifyRequests()
{
    static const char label[] = "Blockifying";

    orderlist_t blockify_orders;

    // Note: using __gnu_parallel in this sort() will crash the program.
    std::stable_sort(schedule.begin(), schedule.end(),
       std::mem_fun_ref(&schedule_item::CompareSchedulingOrder) );

    uint_fast64_t total_size = 0, blocks_total = 0;
    uint_fast64_t total_done = 0, blocks_done  = 0;
    for(size_t a=0; a<schedule.size(); ++a)
    {
        uint_fast64_t size = schedule[a].GetDataSource()->size();
        long blocksize     = schedule[a].GetBlockSize();
        total_size   += size;
        blocks_total += CalcSizeInBlocks(size, blocksize);
    }

    for(ssize_t a=0; a < (ssize_t) schedule.size(); ++a)
    {
        schedule_item& s = schedule[a];

        datasource_t* source  = s.GetDataSource();
        unsigned char* target = s.GetBlockTarget();
        uint_fast64_t nbytes  = source->size();
        uint_fast64_t blocksize = s.GetBlockSize();

        if(DisplayBlockSelections)
            std::printf("%s\n", source->getname().c_str());

        ssize_t HandlingCounter = BlockifyAmount2;

        source->open();
        uint_fast64_t offset = 0;
        while(nbytes > 0)
        {
            DisplayProgress(label, total_done, total_size, blocks_done, blocks_total);

            uint_fast64_t eat = std::min(blocksize, nbytes);

            /* TODO: Threading, possibly? */

            if(unlikely(source == 0)) throw std::logic_error("source should not be 0");
            AddOrder(blockify_orders, source->read(eat), offset, target);

            nbytes -= eat;
            offset += eat;
            target += BLOCKNUM_SIZE_BYTES(); // where pointer will be written to.
            total_done += eat;
            ++blocks_done;

            if(--HandlingCounter <= 0)
                { HandlingCounter = BlockifyAmount2;
                  HandleOrders(blockify_orders, BlockifyAmount1); }
        }
        source->close();
        HandleOrders(blockify_orders, BlockifyAmount1);

        schedule.erase(schedule.begin() + a);
        --a;
    }
    schedule.clear();

    DisplayProgress(label, total_done, total_size, blocks_done, blocks_total);

    HandleOrders(blockify_orders, 0);
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
    block_index.clear();
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
