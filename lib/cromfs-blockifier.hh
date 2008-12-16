#include "endian.hh"
#include "datasource.hh"
#include "cromfs-blockindex.hh"
#include "cromfs-fblockfun.hh"
#include "autoptr"

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
          last_autoindex_length(), block_index()
    {
        /* Set up the global pointer to our block_index
         * so that cromfs_blockfun.cc can access it in
         * the case of emergency.
         */
        block_index_global = &block_index;
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
        cromfs_blocknum_t blocknum;
        cromfs_block_internal block;
        BlockIndexHashType crc;
    public:
        ReusingPlan(bool) : success(false),blocknum(NO_BLOCK),block(),crc(0) { }

        ReusingPlan(BlockIndexHashType c, cromfs_blocknum_t bn)
            : success(true),blocknum(bn),block(),crc(c) { }

        ReusingPlan(BlockIndexHashType c, const cromfs_block_internal& b)
            : success(true),blocknum(NO_BLOCK),block(b),crc(c) { }

        operator bool() const { return success; }
    };

    /* Method for creating a reusing plan. May not work
     * if there's nothing to reuse. In that case, a plan
     * that evaluates into false is returned.
     */
    const ReusingPlan CreateReusingPlan(
        const unsigned char* data, uint_fast32_t size,
        const BlockIndexHashType crc,
        bool PreferRealIndex = true);

    const ReusingPlan CreateReusingPlan(
        const std::vector<unsigned char>& data,
        const BlockIndexHashType crc,
        bool PreferRealIndex = true)
    { return CreateReusingPlan(&data[0], data.size(), crc, PreferRealIndex); }

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
        BlockIndexHashType            crc;

        WritePlan(cromfs_fblocknum_t f, const AppendInfo& a,
                  const BoyerMooreNeedleWithAppend& d, BlockIndexHashType c)
            : fblocknum(f), appended(a), data(d), crc(c) { }
    };

    /* Create a plan on appending to a fblock. It will always work. */
    /* But which fblock to append to? */
    const WritePlan CreateWritePlan(const BoyerMooreNeedleWithAppend& data, BlockIndexHashType crc,
        overlaptest_history_t& history) const;

    /* Execute an appension plan */
    cromfs_blocknum_t Execute(const WritePlan& plan, bool DoUpdateBlockIndex = true);

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

    bool block_is(const cromfs_block_internal& block,
                  const std::vector<unsigned char>& data) const
    {
        return block_is(block, &data[0], data.size());
    }

    bool block_is(const cromfs_block_internal& block,
                  const unsigned char* data,
                  uint_fast32_t data_size) const;

    bool block_is(const cromfs_blocknum_t blocknum,
                  const std::vector<unsigned char>& data) const
    {
        return block_is(blocks[blocknum], &data[0], data.size());
    }

    bool block_is(const cromfs_blocknum_t blocknum,
                  const unsigned char* data,
                  uint_fast32_t data_size) const
    {
        return block_is(blocks[blocknum], data, data_size);
    }

    cromfs_blocknum_t CreateNewBlock(const cromfs_block_internal& block)
    {
        cromfs_blocknum_t blocknum = blocks.size();
        blocks.push_back(block);
        return blocknum;
    }

    /* An order is a concept which appends data (block) into some
     * fblock and yields a block number.
     *
     * All files (and inotab and rootdir) are split into blocks
     * and made orders of. The block numbers are written into
     * the inode.
     *
     * The scheduler splits each file into blocks, and juggles
     * N blocks at a time in order to decide the best way to
     * place them into fblocks. In the end, everything will be
     * written, but the order may be arbitrary.
     *
     * However, at most BlockifyAmount1 orders
     * are kept pending at any given time.
     */
    struct individual_order
    {
    public:
        // The data that must be eventually found in _some_ fblock
        const std::vector<unsigned char> data;
        // hash calculated from the data
        const BlockIndexHashType crc;

        // Where to write the blocknum once it's decided
        unsigned char* const target;

        /* needle is behind an autoptr, so that it doesn't always
         * need to be initialized. */
        autoptr<BoyerMooreNeedleWithAppend> needle;

        // A sorting key, decided by EvaluateBlockifyOrders()
        float badness;

        overlaptest_history_t minimum_tested_positions;
    public:
        individual_order(const std::vector<unsigned char>& d,
                         unsigned char* t)
            : data(d),
              crc(BlockIndexHashCalc(&d[0], d.size())),
              target(t),
              needle(),
              badness(0),
              minimum_tested_positions()
        {
        }

        void MakeNeedle()
        {
            needle = new BoyerMooreNeedleWithAppend(data);
        }

        void Write(cromfs_blocknum_t num)
        {
            Wn(target, num, BLOCKNUM_SIZE_BYTES());
        }

        bool CompareOrder(const individual_order& b) const
        {
            // primary sort key: badness.
            if(badness != b.badness) return badness < b.badness;

            // When they are equal, try to maintain old order.
            // This usually correlates with file position & file index:
            return target < b.target;
            //return offset < b.offset;

            return false;
        }

        individual_order(const individual_order& b)
            : data(b.data), crc(b.crc), target(b.target),
              needle(b.needle), badness(b.badness),
              minimum_tested_positions(b.minimum_tested_positions) // -Weffc++
        { }
    private:
        individual_order& operator= (const individual_order&);
    };
    typedef std::list<individual_order> orderlist_t;

private:
    // add order
    void AddOrder(
        orderlist_t& blockify_orders,
        const std::vector<unsigned char>& data,
        uint_fast64_t offset,
        unsigned char* target);

    // handle pending orders
    void HandleOrders(orderlist_t& blockify_orders, ssize_t max_remaining_orders);

    // assign "badness" for each order to assist in sorting them
    void EvaluateBlockifyOrders(orderlist_t& blockify_orders);

public:
    /* Adds a new blockifying request. */
    // Note: The blockifier will take care of deallocating the datasource_t.
    void ScheduleBlockify(
        datasource_t* source,
        SchedulerDataClass dataclass,
        unsigned char* blocknum_target,
        long BlockSize);

    /* Flushes all blockifying requests so far. */
    void FlushBlockifyRequests();

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
            return ((int)dataclass < (int)(b.dataclass));
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

    std::deque<schedule_item> schedule;

public:
    // Data locators written into filesystem. Indexed by block number.
    std::vector<cromfs_block_internal> blocks;

    // The fblocks written into filesystem. Indexed by data locators.
    mkcromfs_fblockset fblocks;

    /* For each bsize differently. */
    std::vector< std::map<long,size_t> > last_autoindex_length;

    // This is the index used for two purposes:
    //   Discovering identical blocks (reuse of the data locator)
    //     Reuses the old block number
    //   Autoindex of fblocks (full overlap without substring search)
    //     Yields a new block number
    block_index_type block_index;

private:
    cromfs_blockifier(const cromfs_blockifier& );
    void operator=(const cromfs_blockifier& );
};
