#include "endian.hh"
#include "datasource.hh"
#include "cromfs-blockindex.hh"
#include "cromfs-fblockfun.hh"
#include "fsballocator.hh"
#include "newhash.h"
#include "autoptr"

#include <map>
#include <list>
#include <deque>

/****************************************************/
/* Block scheduler. It stores the scattered requests *
 * to read files and write data, and then does them  *
 * later together.                                   *
 *****************************************************/
class cromfs_blockifier
{
public:
    cromfs_blockifier()
        : schedule(), blocks(), fblocks(),
          last_autoindex_length(), autoindex()
    {
        /* Set up the global pointer to our block_index
         * so that cromfs_fblockfun.cc can access it in
         * the case of emergency.
         */
        //block_index_global = &block_index;
        fblockset_global   = &fblocks;
    }
    ~cromfs_blockifier()
    {
        for(size_t a=0; a<fblocks.size(); ++a)
            fblocks[a].Delete();
    }

public:
    typedef std::map<cromfs_fblocknum_t, size_t> overlaptest_history_t;

private:
    /*****************************************************/
    /* The different types of plan for storing the data. */
    /*****************************************************/

    //
    // Plan for reusing already stored block data
    //

    struct ReusingPlan
    {
    public:
        bool success;
        cromfs_block_internal block;
        newhash_t crc;
    public:
        ReusingPlan(bool) : success(false),block(),crc(0) { }

        ReusingPlan(newhash_t c, const cromfs_block_internal& b)
            : success(true),block(b),crc(c) { }

        operator bool() const { return success; }
    };

    /* Method for creating a reusing plan. May not work
     * if there's nothing to reuse. In that case, a plan
     * that evaluates into false is returned.
     */
    const ReusingPlan CreateReusingPlan(
        const unsigned char* data, uint_fast32_t size,
        const newhash_t crc);

    /* Execute a reusing plan */
    cromfs_blocknum_t Execute(const ReusingPlan& plan, uint_fast32_t blocksize);

    //
    // Plan for writing new data
    //

    struct WritePlan
    {
        cromfs_fblocknum_t fblocknum;
        AppendInfo         appended;
        const BoyerMooreNeedleWithAppend& data;
        newhash_t                crc;

        WritePlan(cromfs_fblocknum_t f, const AppendInfo& a,
                  const BoyerMooreNeedleWithAppend& d, newhash_t c)
            : fblocknum(f), appended(a), data(d), crc(c) { }
    };

    /* Create a plan on appending to a fblock. It will always work. */
    /* But which fblock to append to? */
    const WritePlan CreateWritePlan(const BoyerMooreNeedleWithAppend& data, newhash_t crc,
        overlaptest_history_t& history) const;

    /* Execute an appension plan */
    cromfs_blocknum_t Execute(const WritePlan& plan);

    /*********************************************************************/
    /* Generic methods for complementing fblocks, blocks and block_index */
    /*********************************************************************/

    /* Autoindex new data in this fblock */
    void AutoIndex(const cromfs_fblocknum_t fblocknum,
        uint_fast32_t old_raw_size,
        uint_fast32_t new_raw_size,
        uint_fast32_t bsize);

    void TryAutoIndex(const cromfs_fblocknum_t fblocknum,
        const unsigned char* ptr,
        uint_fast32_t bsize,
        uint_fast32_t startoffs);

    void AutoIndexBetween(const cromfs_fblocknum_t fblocknum,
        const unsigned char* ptr,
        uint_fast32_t min_offset,
        uint_fast32_t max_size,
        uint_fast32_t bsize,
        uint_fast32_t stepping);

    void PredictiveAutoIndex(
        cromfs_fblocknum_t fblocknum,
        uint_fast32_t startoffs,
        uint_fast32_t blocksize);


    cromfs_blocknum_t CreateNewBlock(const cromfs_block_internal& block)
    {
        cromfs_blocknum_t blocknum = blocks.size();
        blocks.push_back(block);
        return blocknum;
    }

public:
    /* Adds a new blockifying request. */
    // Note: The blockifier will take care of deallocating the datasource_t.
    void ScheduleBlockify(
        datasource_t* source,
        SchedulerDataClass dataclass,
        unsigned char* blocknum_target,
        long BlockSize);

    /* Flushes all blockifying requests so far. */
    void FlushBlockifyRequests(const char* purpose);

    /* Method for reducing RAM use when no more blockifying requests are expected. */
    void NoMoreBlockifying();

    /* Switches on some options based on data. */
    void EnablePackedBlocksIfPossible();

private:
    struct schedule_item
    {
        /* This structures stores a request to blockify()
         * data from given source.
         * The blocks will be written into the given target.
         */
        schedule_item(
            datasource_t* src, SchedulerDataClass dc,
            unsigned char* tgt, long bsize)
            : source(src), target(tgt), dataclass(dc), blocksize(bsize)
        {
        }

        inline datasource_t* GetDataSource() const { return source; }
        const std::string GetName() const { return source->getname(); }
        long GetBlockSize() const { return blocksize; }

        inline unsigned char* GetBlockTarget() const { return target; }

        bool CompareSchedulingOrder(const schedule_item& b) const
        {
            if(dataclass != b.dataclass) return ((int)dataclass < (int)(b.dataclass));
            return source->getname() < b.source->getname();
        }

        schedule_item(const schedule_item& b)
            : source(b.source), target(b.target),
              dataclass(b.dataclass), blocksize(b.blocksize) // -Weffc++
        {
        }

        schedule_item& operator= (const schedule_item& b)
        {
            if(&b != this)
            {
                source = b.source;
                target = b.target;
                dataclass = b.dataclass;
                blocksize = b.blocksize;
            }
            return *this;
        }

    private:
        autoptr<datasource_t> source;

        unsigned char*     target;
        SchedulerDataClass dataclass;
        long               blocksize;
    };

    std::vector<schedule_item> schedule;

public:
    // Data locators written into filesystem. Indexed by block number.
    std::vector<cromfs_block_internal> blocks;

    // The fblocks written into filesystem. Indexed by data locators.
    mkcromfs_fblockset fblocks;

    /* For each bsize differently. */
    std::map<cromfs_fblocknum_t,
             std::map<long,size_t,
                      std::less<long>,
                      FSBAllocator<std::pair<const long,size_t> >
                     >,
             std::less<cromfs_fblocknum_t>,
             FSBAllocator<int> > last_autoindex_length;

    // The autoindex
    typedef std::multimap<newhash_t, cromfs_block_internal, std::less<newhash_t>,
                          FSBAllocator<std::pair<const newhash_t, cromfs_block_internal> >
                         > autoindex_base;
    class autoindex_t : public
            std::multimap<newhash_t, cromfs_block_internal, std::less<newhash_t>,
                          FSBAllocator<std::pair<const newhash_t, cromfs_block_internal> >
                         >
    {
    public:
        struct find_index_t
        {
            autoindex_base::const_iterator i;
            bool first, last;

            find_index_t() : i(), first(true), last(false) { }

            find_index_t& operator++() { if(!last) ++i; first=false; return *this; }
            find_index_t operator++(int) const { find_index_t res(*this); ++res; return res; }
        };
    public:
        autoindex_t() : autoindex_base(), added(0), deleted(0) { }

        void Del(newhash_t index, const cromfs_block_internal& b)
        {
            for(autoindex_base::iterator i = lower_bound(index);
                i != end() && i->first == index;
                ++i)
            {
                if(i->second == b) { erase(i); ++deleted; break; }
            }
        }
        void Add(newhash_t index, const cromfs_block_internal& b)
        {
            insert(std::make_pair(index, b));
            ++added;
        }
        bool Find(newhash_t index, cromfs_block_internal& res, find_index_t& nmatch) const
        {
            if(nmatch.first)
            {
                nmatch.i     = lower_bound(index);
                nmatch.first = false;
            }
            if(nmatch.i == end()) { nmatch.last = true; return false; }
            if(nmatch.i->first != index) { nmatch.last = true; return false; }
            res = nmatch.i->second;
            return true;
        }
        const std::string GetStatistics() const;
    private:
        size_t added, deleted;
    };
    autoindex_t autoindex;

private:
    cromfs_blockifier(const cromfs_blockifier& );
    void operator=(const cromfs_blockifier& );
};
