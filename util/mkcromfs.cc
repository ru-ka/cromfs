#define _LARGEFILE64_SOURCE
#define __STDC_CONSTANT_MACROS

#include "../cromfs-defs.hh"
#include "autoptr"

#include <vector>
#include <cstdio>
#include <cstdlib>
#include <map>
#include <set>
#include <algorithm>
#include <list>

#ifdef USE_HASHMAP
# include <ext/hash_map>
# include "hash.hh"
#endif

#include <unistd.h>
#include <dirent.h>
#include <fcntl.h>
#include <getopt.h>
#include <errno.h>
#include <sys/time.h>

#include <signal.h>

#include <sys/vfs.h> /* for statfs64 */

#include "mkcromfs_sets.hh"

#include "lzma.hh"
#include "datasource.hh"
#include "fblock.hh"
#include "crc32.h"
#include "util.hh"
#include "fnmatch.hh"
#include "bwt.hh"
#include "mtf.hh"

/* Settings */
#include "mkcromfs_sets.hh"
int LZMA_HeavyCompress = 1;
bool DecompressWhenLookup = false;
bool FollowSymlinks = false;
unsigned RandomCompressPeriod = 20;
uint_fast32_t MinimumFreeSpace = 16;
uint_fast32_t AutoIndexPeriod = 256;
uint_fast32_t MaxFblockCountForBruteForce = 1;
bool MayPackBlocks = true;
bool MayAutochooseBlocknumSize = true;
bool MaySuggestDecompression = true;

// Number of blockifys to keep in buffer, hoping for optimal sorting
size_t BlockifyAmount1 = 64;
// Number of blockifys to add to buffer at once
size_t BlockifyAmount2 = 1;
// 0 = nope. 1 = yep, 2 = yes, and try combinations too
int TryOptimalOrganization = 0;


long FSIZE = 2097152;
long BSIZE = 65536;
//uint_fast32_t MaxSearchLength = FSIZE;


double RootDirInflateFactor = 1;
double InotabInflateFactor  = 1;
double BlktabInflateFactor  = 1;

static MatchingFileListType exclude_files;

/* Order in which to parse different types of directory entries */
static struct
{
    char Link, Dir, Other;
} DirParseOrder = { 2, 3, 1 };

/* Order in which to blockify different types of data */
typedef char SchedulerDataClass;
static struct
{
    char Symlink, Directory, File, Inotab;
} DataClassOrder = { 1, 2, 3, 4 };

static bool MatchFile(const std::string& entname)
{
    return !MatchFileFrom(entname, exclude_files, false);
}

static bool DisplayBlockSelections = true;
static bool DisplayFiles = true;
static bool DisplayEndProcess = true;

static uint_fast32_t storage_opts = 0x00000000;

#define BLOCKNUM_SIZE_BYTES() \
   (4 - 1*!!(storage_opts & CROMFS_OPT_24BIT_BLOCKNUMS) \
      - 2*!!(storage_opts & CROMFS_OPT_16BIT_BLOCKNUMS) )


#define NO_BLOCK ((cromfs_blocknum_t)~UINT64_C(0))

struct mkcromfs_block_location: public cromfs_block_internal
{
    cromfs_blocknum_t  blocknum __attribute__((packed));

    mkcromfs_block_location(
        cromfs_fblocknum_t fbnum, uint_fast32_t sofs,
        cromfs_blocknum_t b)
            : cromfs_block_internal(), blocknum(b)
    {
        cromfs_block_internal::define(fbnum, sofs, BSIZE,FSIZE);
    }

    mkcromfs_block_location(
        const cromfs_block_internal& bi,
        cromfs_blocknum_t b)
            : cromfs_block_internal(bi), blocknum(b)
    {
    }
} __attribute__((packed));

typedef std::pair<dev_t,ino_t> hardlinkdata;

class cromfs
{
private:
    uint_fast64_t amount_blockdata;
    uint_fast64_t amount_fblockdata;
    mutable int ExitStatus;

    /* These data will be written into the filesystem. */
    std::vector<cromfs_block_internal> blocks;
    std::vector<mkcromfs_fblock> fblocks;
    std::vector<unsigned char> inotab;
    std::vector<unsigned char> raw_root_inode;
    std::vector<unsigned char> raw_inotab_inode;
    uint_least64_t bytes_of_files;

public:
    cromfs()
      : amount_blockdata(0),
        amount_fblockdata(0),
        ExitStatus(0),
        blocks(),
        fblocks(),
        raw_root_inode(),
        raw_inotab_inode(),
        bytes_of_files(0),
        scheduler(*this)
    {
    }
    ~cromfs()
    {
        for(unsigned a=0; a<fblocks.size(); ++a)
        {
            fblocks[a].Delete();
        }
    }
    
    int GetExitStatus() const { return ExitStatus; }
    
    /***************************************/
    /* Start here: Walk through some path. */
    /***************************************/
    
    void WalkRootDir(const std::string& path)
    {
        if(true) // scope
        {
            cromfs_inode_internal rootdir;
            
            cromfs_dirinfo dirinfo = WalkDir(path);
            
            if(DisplayFiles)
            {
                std::printf("Paths scanned, now blockifying\n");
            }
            
            rootdir.mode     = S_IFDIR | 0555;
            rootdir.time     = time(NULL);
            rootdir.links    = dirinfo.size();
            rootdir.uid       = 0;
            rootdir.gid       = 0;
            
            mkcromfs_blockify_schedule* schedule =
                ScheduleBlockify(
                    new datasource_vector(encode_directory(dirinfo), "root dir"),
                    rootdir,
                    DataClassOrder.Directory);

            raw_root_inode = encode_inode(rootdir);
            
            schedule->RepointAsRawInode(raw_root_inode, 0);
        }
        if(true) // scope
        {
            cromfs_inode_internal inotab_inode;
            inotab_inode.mode = storage_opts;
            fflush(stdout); fflush(stderr);
            //fprintf(stderr, "Writing storage options %X\n", storage_opts);
            fflush(stdout); fflush(stderr);
            inotab_inode.time = time(NULL);
            inotab_inode.links = 1;
            
            FlushBlockifyRequests();
              
            /* Before this line, all pending Blockify requests must be completed,
             * because they will write data into inotab.
             */
            mkcromfs_blockify_schedule* schedule =
                ScheduleBlockify(
                    new datasource_vector(inotab, "inotab"),
                    inotab_inode,
                    DataClassOrder.Inotab);
                
            if(DisplayEndProcess)
            {
                std::printf("Uncompressed inode table is %s (stored in fblocks, compressed).\n",
                    ReportSize(inotab_inode.bytesize).c_str());
            }

            raw_inotab_inode = encode_inode(inotab_inode);

            schedule->RepointAsRawInode(raw_inotab_inode, 0);
        }
        FlushBlockifyRequests();
        EnablePackedBlocksIfPossible();
        W32(&raw_inotab_inode[0], storage_opts);
    }

    /***********************************************/
    /* End here: Write the filesystem into a file. */
    /***********************************************/
    
    void WriteTo(int fd) const
    {
#if 0 /* for speed profiling */
        uint_fast64_t tmp=0;
        for(unsigned a=0; a<fblocks.size(); ++a)
            tmp += fblocks[a].get_raw().size();
        std::printf("fblock size total = %llu\n", tmp);
        return;
#endif

        /* Before this line, all pending Blockify requests must be completed,
         * because they will write data into fblocks and blkdata and inotab.
         */

        if(DisplayEndProcess)
        {
            std::printf("Compressing inodes of inotab (%s) and rootdir (%s)...\n",
                ReportSize(raw_inotab_inode.size()).c_str(),
                ReportSize(raw_root_inode.size()).c_str()
                    );
        }
        
        std::vector<unsigned char>
            compressed_inotab_inode,
            compressed_root_inode;
        
        if(LZMA_HeavyCompress==2)
        {
            compressed_inotab_inode = LZMACompressHeavy(raw_inotab_inode, "raw_inotab_inode");
            compressed_root_inode   = LZMACompressHeavy(raw_root_inode, "raw_root_inode");
        }
        else if(LZMA_HeavyCompress)
        {
            compressed_inotab_inode = LZMACompressAuto(raw_inotab_inode, "raw_inotab_inode");
            compressed_root_inode   = LZMACompressAuto(raw_root_inode, "raw_root_inode");
        }
        else
        {
            compressed_inotab_inode = LZMACompress(raw_inotab_inode);
            compressed_root_inode   = LZMACompress(raw_root_inode);
        }
        
        unsigned onesize = (storage_opts & CROMFS_OPT_PACKED_BLOCKS) ? 4 : 8;
        if(DisplayEndProcess)
        {
            std::printf("Compressing %u block records (%u bytes each)...",
                (unsigned)blocks.size(), onesize);
            fflush(stdout);
        }

        std::vector<unsigned char> raw_blktab(blocks.size() * onesize);
        if(storage_opts & CROMFS_OPT_PACKED_BLOCKS)
            for(unsigned a=0; a<blocks.size(); ++a)
            {
                uint_fast32_t fblocknum = blocks[a].get_fblocknum(BSIZE,FSIZE);
                uint_fast32_t startoffs = blocks[a].get_startoffs(BSIZE,FSIZE);
                
                //fprintf(stderr, "Writing P block %u = %u:%u\n", a,fblocknum,startoffs);
                
                W32(&raw_blktab[a*onesize], fblocknum * FSIZE + startoffs);
            }
        else
            for(unsigned a=0; a<blocks.size(); ++a)
            {
                uint_fast32_t fblocknum = blocks[a].get_fblocknum(BSIZE,FSIZE);
                uint_fast32_t startoffs = blocks[a].get_startoffs(BSIZE,FSIZE);

                //fprintf(stderr, "Writing NP block %u = %u:%u\n", a,fblocknum,startoffs);

                W32(&raw_blktab[a*onesize+0], fblocknum);
                W32(&raw_blktab[a*onesize+4], startoffs);
            }
        
        if(LZMA_HeavyCompress==2)
        {
            raw_blktab = LZMACompressHeavy(raw_blktab, "raw_blktab");
        }
        /*else if(LZMA_HeavyCompress)
        {
            raw_blktab = LZMACompressAuto(raw_blktab, "raw_blktab");
        }*/
        else
        {
            const unsigned blktab_periodicity
                = (storage_opts & CROMFS_OPT_PACKED_BLOCKS) ? 2 : 3;
            raw_blktab = LZMACompress(raw_blktab,
                blktab_periodicity,
                blktab_periodicity,
                0);
        }

        if(DisplayEndProcess)
        {
            std::printf(" compressed into %s\n", ReportSize(raw_blktab.size()).c_str()); fflush(stdout);
        }

        cromfs_superblock_internal sblock;
        sblock.sig          = CROMFS_SIGNATURE;
        sblock.rootdir_size = compressed_root_inode.size();
        sblock.inotab_size  = compressed_inotab_inode.size();
        sblock.blktab_size  = raw_blktab.size();
        
        sblock.rootdir_room = sblock.rootdir_size * RootDirInflateFactor;
        sblock.inotab_room  = sblock.inotab_size  * InotabInflateFactor;
        sblock.blktab_room  = sblock.blktab_size  * BlktabInflateFactor;
        
        sblock.fsize   = FSIZE;
        sblock.bsize = BSIZE;
        sblock.bytes_of_files          = bytes_of_files;
        
        sblock.SetOffsets();
        
        cromfs_superblock_internal::BufferType Superblock;
        sblock.WriteToBuffer(Superblock);
        
        lseek64(fd, 0, SEEK_SET);
        
        write(fd, Superblock, sblock.GetSize());
        
        //fprintf(stderr, "root goes at %llX\n", lseek64(fd,0,SEEK_CUR));
        SparseWrite(fd, &compressed_root_inode[0],   compressed_root_inode.size(), sblock.rootdir_offs);
        //fprintf(stderr, "inotab goes at %llX\n", lseek64(fd,0,SEEK_CUR));
        SparseWrite(fd, &compressed_inotab_inode[0], compressed_inotab_inode.size(), sblock.inotab_offs);

        SparseWrite(fd, &raw_blktab[0], raw_blktab.size(), sblock.blktab_offs);
        
        uint_fast64_t compressed_total = 0;
        uint_fast64_t uncompressed_total = 0;
        
        lseek64(fd, sblock.fblktab_offs, SEEK_SET);
            
        for(unsigned a=0; a<fblocks.size(); ++a)
        {
            if(DisplayEndProcess)
            {
                std::printf("\rWriting fblock %u/%u...",
                    a, (unsigned)fblocks.size());
                std::fflush(stdout);
            }
            
            char Buf[64];
            std::vector<unsigned char> fblock_lzma, fblock_raw;
            
            if(storage_opts & (CROMFS_OPT_USE_BWT | CROMFS_OPT_USE_MTF))
            {
                fblock_raw = fblock_lzma = fblocks[a].get_raw();
                if(storage_opts & CROMFS_OPT_USE_BWT)
                    fblock_lzma = BWT_encode_embedindex(fblock_lzma);
                if(storage_opts & CROMFS_OPT_USE_MTF)
                    fblock_lzma = MTF_encode(fblock_lzma);
                if(LZMA_HeavyCompress == 2)
                    fblock_lzma = LZMACompressHeavy(fblock_lzma, "fblock");
                else if(LZMA_HeavyCompress)
                    fblock_lzma = LZMACompressAuto(fblock_lzma, "fblock");
                else fblock_lzma = LZMACompress(fblock_lzma);
            }
            else
            {
                fblocks[a].get(fblock_raw, fblock_lzma);
            }
            
            if(DisplayEndProcess)
            {
                std::printf(" %u bytes (orig %u)      ",
                    (unsigned)fblock_lzma.size(),
                    (unsigned)fblock_raw.size());
            }
            
            W64(Buf, fblock_lzma.size());
            write(fd, Buf, 4);
            
            const uint_fast64_t pos = lseek64(fd, 0, SEEK_CUR);
            SparseWrite(fd, &fblock_lzma[0], fblock_lzma.size(), pos);
            
            if(storage_opts & CROMFS_OPT_SPARSE_FBLOCKS)
            {
                if(fblock_lzma.size() > (size_t)FSIZE)
                {
                    std::printf("\n");
                    std::fflush(stdout);
                    std::fprintf(stderr,
                        "Error: This filesystem cannot be sparse, because a compressed fblock\n"
                        "       actually became larger than the decompressed one.\n"
                        "       Sorry. Try the 'minfreespace' option if it helps.\n"
                     );
                    ExitStatus=1;
                    return;
                }
                lseek64(fd, pos + FSIZE, SEEK_SET);
            }
            else
            {
                lseek64(fd, pos + fblock_lzma.size(), SEEK_SET);
            }
            
            std::fflush(stdout);
            
            compressed_total   += fblock_lzma.size();
            uncompressed_total += fblock_raw.size();
        }
        
        ftruncate64(fd, lseek64(fd, 0, SEEK_CUR));
        
        if(DisplayEndProcess)
        {
            uint_fast64_t file_size = lseek64(fd, 0, SEEK_CUR);

            std::printf(
                "\n%u fblocks were written: %s = %.2f %% of %s\n",
                (unsigned)fblocks.size(),
                ReportSize(compressed_total).c_str(),
                compressed_total * 100.0 / (double)uncompressed_total,
                ReportSize(uncompressed_total).c_str()
               );
            std::printf(
                "Filesystem size: %s = %.2f %% of original %s\n",
                ReportSize(file_size).c_str(),
                file_size * 100.0 / (double)bytes_of_files,
                ReportSize(bytes_of_files).c_str()
                   );
        }
    }

private:
    /*******************/
    /* Private methods */
    /*******************/
    
    /*****************************************************/
    /* Methods for setting on some options based on the  */
    /* data.                                             */
    /*****************************************************/

    void EnablePackedBlocksIfPossible()
    {
        if(MayPackBlocks)
        {
            uint_fast64_t max_blockoffset = FSIZE - BSIZE;
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
                std::printf( "mkcromfs: --16bitblocksnums would have been possible for this filesystem. But you didn't select it.\n");
                if(MayAutochooseBlocknumSize)
                    std::printf("          (mkcromfs did not automatically do this, because the block merging went better than estimated.)\n");
            }
        }
        else if(blocks.size() < 0x1000000UL)
        {
            if(!(storage_opts & CROMFS_OPT_24BIT_BLOCKNUMS))
            {
                std::printf( "mkcromfs: --24bitblocksnums would have been possible for this filesystem. But you didn't select it.\n");
                if(MayAutochooseBlocknumSize)
                    std::printf("          (mkcromfs did not automatically do this, because the block merging went better than estimated.)\n");
            }
        }
    }
    
    /*****************************************************/
    /* The different types of plan for storing the data. */
    /*****************************************************/

    struct ReusingPlan /* Plan for reusing already stored block data */
    {
    public:
        bool success;
        cromfs_blocknum_t blocknum;
        const mkcromfs_block_location* block;
        crc32_t crc;
    public:
        ReusingPlan(bool) : success(false),blocknum(NO_BLOCK),block(0),crc(0) { }
        
        ReusingPlan(crc32_t c, const mkcromfs_block_location& b,
                    cromfs_blocknum_t bn)
            : success(true),blocknum(bn),block(&b),crc(c) { }
        
        operator bool() const { return success; }
    };
    struct WritePlan /* Plan for writing new data */
    {
        cromfs_fblocknum_t fblocknum;
        AppendInfo         appended;
        const BoyerMooreNeedle& data;
        crc32_t            crc;
        
        WritePlan(cromfs_fblocknum_t f, const AppendInfo& a,
                  const BoyerMooreNeedle& d, crc32_t c)
            : fblocknum(f), appended(a), data(d), crc(c) { }
    };
    
    /*********************************************/
    /* The methods for creating different plans. */
    /*********************************************/

    /* Index for reusable material */
#ifdef USE_HASHMAP
    typedef __gnu_cxx::hash_multimap<crc32_t, mkcromfs_block_location> block_index_t;
#else
    typedef std::multimap<crc32_t, mkcromfs_block_location> block_index_t;
#endif
    block_index_t block_index;

    /* An index which sorts the fblocks in ascending order of free space */
    typedef std::multimap<int_fast32_t/*room*/, cromfs_fblocknum_t> fblock_index_type;
    fblock_index_type fblock_index;

    /* Method for creating a reusing plan. May not work
     * if there's nothing to reuse. In that case, a plan
     * that evaluates into false is returned.
     */
    const ReusingPlan CreateReusingPlan(
        const std::vector<unsigned char>& data,
        const crc32_t crc)
    {
        /* Use CRC32 to find the identical block. */
        block_index_t::iterator i = block_index.find(crc);
        if(i != block_index.end())
        {
            /* Identical CRC was found */
            /* It may be found more than once, and the finds may be
             * false positives. Hence, we must verify each instance.
             */
            for(;;)
            {
                cromfs_blocknum_t& blocknum = i->second.blocknum;
                const mkcromfs_block_location& block = i->second;
                
                if(!block_is(block, data))
                {
                    /* Didn't actually match. Check more. */
                    ++i;
                    if(i == block_index.end() || i->first != crc) break;
                    continue;
                }
                
                /* Match! */
                return ReusingPlan(crc, block, blocknum);
            }
        }
        /* Didn't find match. */
        return ReusingPlan(false);
    }
    
    /* Create a plan on appending to a fblock. It will always work. */
    /* But which fblock to append to? */
    const WritePlan CreateWritePlan(const BoyerMooreNeedle& data, crc32_t crc,
        uint_fast32_t within_last_n_bytes = FSIZE) const
    {
        /* First check if we can write into an existing fblock. */
        if(true)
        {
            std::vector<cromfs_fblocknum_t> candidates;
            candidates.reserve(fblocks.size());
            
            unsigned priority_candidates = 0;
            
            /* First candidate: The fblock that we would get without brute force. */
            /* I.e. the fblock with smallest fitting hole. */
            {fblock_index_type::const_iterator i = fblock_index.lower_bound(data.size());
            if(i != fblock_index.end())
            {
                candidates.push_back(i->second);
                ++priority_candidates;
            }}
            
            /* Next candidates: last N (up to MaxFblockCountForBruteForce) */
            cromfs_fblocknum_t j = fblocks.size();
            while(j > 0 && candidates.size() < MaxFblockCountForBruteForce)
            {
                --j;
                if(!candidates.empty() && j != candidates[0])
                {
                    candidates.push_back(j);
                    ++priority_candidates;
                }
            }
            
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
            std::random_shuffle(candidates.begin()+priority_candidates, candidates.end());
            
            cromfs_fblocknum_t smallest_fblock = 0;
            uint_fast32_t smallest_adds = 0;
            AppendInfo smallest_appended;
            
            bool found_candidate = false;
            
            /* Check each candidate (up to MaxFblockCountForBruteForce)
             * for the fit which reuses the maximum amount of data.
             */
            for(unsigned a=0; a<candidates.size(); ++a)
            {
                if(a >= MaxFblockCountForBruteForce && found_candidate) break;
                
                cromfs_fblocknum_t fblocknum = candidates[a];
                const mkcromfs_fblock& fblock = fblocks[fblocknum];
                
                //printf("?"); fflush(stdout);
                uint_fast32_t minimum_pos =
                    std::max(0l, (long)fblock.size() - (long)within_last_n_bytes);
                AppendInfo appended = fblock.AnalyzeAppend(data, minimum_pos);
                
                uint_fast32_t this_adds = appended.AppendedSize - appended.OldSize;
                uint_fast32_t overlap_size = data.size() - this_adds;

                //printf("[cand %u:%u]", (unsigned)fblocknum, (unsigned)this_adds);
                //fflush(stdout);
                
                bool this_is_good =
                    overlap_size
                    ? (appended.AppendedSize < FSIZE)
                    : (appended.AppendedSize < FSIZE - MinimumFreeSpace);
                
                if(found_candidate && this_adds >= smallest_adds)
                    this_is_good = false;
                
                if(this_is_good)
                {
                    found_candidate = true;
                    smallest_fblock = fblocknum;
                    smallest_adds = this_adds;
                    smallest_appended = appended;
                    //printf("[!]");
                    if(smallest_adds == 0) break; /* couldn't get better */
                }
            }
            
            /* Utilize the finding, if it's an overlap,
             * or it's an appension into a fblock that still has
             * so much room that it doesn't conflict with MinimumFreeSpace.
             * */
            if(found_candidate)
            {
                const cromfs_fblocknum_t fblocknum = smallest_fblock;
                AppendInfo appended = smallest_appended;
                
                /* This is the plan. */
                return WritePlan(fblocknum, appended, data, crc);
            }
            
            /* Oh, so it didn't fit anywhere! */
        }

        /* Our plan is then, create a new fblock just for this block! */
        AppendInfo appended;
        appended.SetAppendPos(0, data.size());
        return WritePlan(fblocks.size(), appended, data, crc);
    }
    
    /******************************************************/
    /* Methods for executing the different types of plans */
    /******************************************************/
    
    /* Execute a reusing plan */
    cromfs_blocknum_t Execute(const ReusingPlan& plan)
    {
        cromfs_blocknum_t blocknum = plan.blocknum;
        const mkcromfs_block_location& block = *plan.block;
        
        /* If this match didn't have a real block yet, create one */
        if(blocknum == NO_BLOCK)
        {
            blocknum = CreateNewBlockNoIndex(block);
            if(DisplayBlockSelections)
            {
                std::printf("block %u => [%u @ %u] (autoindex hit) (overlap fully)\n",
                    (unsigned)blocknum,
                    (unsigned)block.get_fblocknum(BSIZE,FSIZE),
                    (unsigned)block.get_startoffs(BSIZE,FSIZE));
            }
        }
        else
        {
            if(DisplayBlockSelections)
            {
                std::printf("block %u == [%u @ %u] (reused block)\n",
                    (unsigned)blocknum,
                    (unsigned)block.get_fblocknum(BSIZE,FSIZE),
                    (unsigned)block.get_startoffs(BSIZE,FSIZE));
            }
        }
        return blocknum;
    }
    
    /* Execute an appension plan */
    cromfs_blocknum_t Execute(const WritePlan& plan, bool DoUpdateBlockIndex = true)
    {
        const cromfs_fblocknum_t fblocknum = plan.fblocknum;
        const AppendInfo& appended         = plan.appended;
        
        fblock_index_type::iterator i = fblock_index.end();
        if(fblocknum >= fblocks.size())
        {
#if DEBUG_FBLOCKINDEX
            std::printf("[new block]");
#endif
            /* Create a new fblock */
            fblocks.resize(fblocknum+1);
            amount_fblockdata += 4; /* size of FBLOCK without data */
        }
        else
        {
            /* Find an iterator from fblock_index, if it exists. */
            i = fblock_index.begin();
            while(i != fblock_index.end() && i->second != fblocknum) ++i;
        }

        if(i != fblock_index.end())
        {
#if DEBUG_FBLOCKINDEX
            std::printf("[erasing %d:%u]\n", (int)i->first, (unsigned)i->second);
#endif
            fblock_index.erase(i);
        }

        UnmapOneRandomlyButNot(fblocknum); /* Free up some resources (address space) */
        CompressOneRandomlyButNot(fblocknum); /* Free up some resources (disk space) */

        mkcromfs_fblock& fblock = fblocks[fblocknum];

        const uint_fast32_t new_data_offset = appended.AppendBaseOffset;
        const uint_fast32_t new_raw_size = appended.AppendedSize;
        const uint_fast32_t old_raw_size = appended.OldSize;

        const int_fast32_t new_remaining_room = FSIZE - new_raw_size;

        if(DisplayBlockSelections)
        {
            std::printf("block %u => [%u @ %u] size now %u, remain %d",
                (unsigned)blocks.size(),
                (unsigned)fblocknum,
                (unsigned)new_data_offset,
                (unsigned)new_raw_size,
                (int)new_remaining_room);
            
            if(new_data_offset < old_raw_size)
            {
                if(new_data_offset + plan.data.size() <= old_raw_size)
                    std::printf(" (overlap fully)");
                else
                    std::printf(" (overlap %d)", (int)(old_raw_size - new_data_offset));
            }
            if(new_remaining_room < 0)
            {
                std::printf(" (OVERUSE)");
            }
        }

        fblock.put_appended_raw(appended, plan.data);
        
        if(DoUpdateBlockIndex)
        {
            AutoIndex(fblocknum, old_raw_size, new_raw_size);
        }
        
        amount_fblockdata += new_raw_size - old_raw_size;
        
        if(DisplayBlockSelections)
        {
            std::printf("\n");
        }
        
        cromfs_block_internal block;
        block.define(fblocknum, new_data_offset, BSIZE,FSIZE);
        
        /* If the block is uncompressed, preserve it fblock_index
         * so that CompressOneRandomly() may pick it some day.
         *
         * Otherwise, store it in the index only if it is still a candidate
         * for crunching more bytes into it.
         */
        if(new_remaining_room >= (int)MinimumFreeSpace)
        {
#if DEBUG_FBLOCKINDEX
            std::printf("[inserting %d:%u]\n", (int)new_remaining_room, (unsigned)result.fblocknum);
#endif
                fblock_index.insert(std::make_pair(new_remaining_room, fblocknum));
        }
        else
        {
#if DEBUG_FBLOCKINDEX
            std::printf("[not inserting %d:%u]\n", (int)new_remaining_room, (unsigned)result.fblocknum);
#endif
        }

        /* Add to block index. */
        if(!DoUpdateBlockIndex)
        {
            return CreateNewBlockNoIndex(block);
        }
        
        return CreateNewBlockAddIndex(block, plan.crc);
    }
    
    /* How many automatic indexes can be done in this amount of data? */
    static const int CalcAutoIndexCount(int_fast32_t raw_size)
    {
        int_fast32_t a = (raw_size - BSIZE + AutoIndexPeriod);
        return a / (int_fast32_t)AutoIndexPeriod;
    }

    /* Autoindex new data in this fblock */
    void AutoIndex(const cromfs_fblocknum_t fblocknum,
        uint_fast32_t old_raw_size,
        uint_fast32_t new_raw_size)
    {
        const mkcromfs_fblock& fblock = fblocks[fblocknum];

        /* Index all new checksum data */
        const int OldAutoIndexCount = std::max(CalcAutoIndexCount(old_raw_size),0);
        const int NewAutoIndexCount = std::max(CalcAutoIndexCount(new_raw_size),0);
        if(NewAutoIndexCount > OldAutoIndexCount && NewAutoIndexCount > 0)
        {
            std::vector<unsigned char> new_raw_data = fblock.get_raw();
            
            for(int count=OldAutoIndexCount+1; count<=NewAutoIndexCount; ++count)
            {
                uint_fast32_t startoffs = AutoIndexPeriod * (count-1);
                if(startoffs + BSIZE > new_raw_size) throw "error";
                /*
                std::printf("\nBlock reached 0x%X->0x%X bytes in size, (%d..%d), adding checksum for 0x%X; ",
                    old_raw_size, new_raw_size,
                    OldAutoMD5Count, NewAutoMD5Count,
                    startoffs);
                */
                const unsigned char* ptr = &new_raw_data[startoffs];
                const crc32_t crc = crc32_calc(ptr, BSIZE);
                
                /* Check if this checksum has already been indexed */
                block_index_t::iterator i = block_index.find(crc);
                if(i != block_index.end())
                {
                    /* Check if one of them matches this data, so that we don't
                     * add the same checksum data twice
                     */
                    for(;;)
                    {
                        if(block_is(i->second, ptr, BSIZE)) goto dont_add_crc;
                        ++i;
                        if(i == block_index.end() || i->first != crc) break;
                    }
                }
                
                block_index.insert(std::make_pair(crc,
                    mkcromfs_block_location(
                        fblocknum, startoffs,
                        NO_BLOCK)));
              dont_add_crc: ;
            }
        }
    }
    
    bool block_is(const mkcromfs_block_location& block,
                  const std::vector<unsigned char>& data) const
    {
        return block_is(block, &data[0], data.size());
    }

    bool block_is(const mkcromfs_block_location& block,
                  const unsigned char* data,
                  uint_fast32_t data_size) const
    {
        return fblocks[block.get_fblocknum(BSIZE,FSIZE)]
                  .compare_raw_portion(data, data_size,
                    block.get_startoffs(BSIZE,FSIZE)) == 0;
    }

    cromfs_blocknum_t CreateNewBlockNoIndex(const cromfs_block_internal& block)
    {
        amount_blockdata += sizeof(block);
        cromfs_blocknum_t blocknum = blocks.size();
        blocks.push_back(block);
        return blocknum;
    }

    cromfs_blocknum_t CreateNewBlockNoIndex(const mkcromfs_block_location& info)
    {
        cromfs_block_internal block(info);
        return CreateNewBlockNoIndex(block);
    }

    cromfs_blocknum_t CreateNewBlockAddIndex
        (const cromfs_block_internal& block,
         const crc32_t crc)
    {
        cromfs_blocknum_t blocknum = CreateNewBlockNoIndex(block);
        block_index.insert(std::make_pair(crc, 
            mkcromfs_block_location(block, blocknum) ));
        return blocknum;
    }
    
    /* A backup of data so that we can simulate an Execute(WritePlan) */
    struct SituationBackup
    {
        size_t n_blocks;
        size_t n_fblocks;
        std::vector<mkcromfs_fblock::undo_t> fblock_state;
        fblock_index_type fblock_index;
        uint_fast64_t amount_blockdata;
        uint_fast64_t amount_fblockdata;
    };
    
    void CreateBackup(SituationBackup& e) const
    {
        e.n_blocks = blocks.size();
        e.n_fblocks = fblocks.size();
        e.fblock_state.reserve(fblocks.size());
        for(unsigned a = 0; a < fblocks.size(); ++a)
            e.fblock_state.push_back(fblocks[a].create_backup());
        e.fblock_index = fblock_index;
        e.amount_blockdata  = amount_blockdata;
        e.amount_fblockdata = amount_fblockdata;
    }
    void RestoreBackup(const SituationBackup& e)
    {
        blocks.resize(e.n_blocks);
        
        for(unsigned a=e.n_fblocks; a<fblocks.size(); ++a)
            fblocks[a].Delete();
        
        fblocks.resize(e.n_fblocks);
        
        for(unsigned a = 0; a < e.fblock_state.size(); ++a)
            fblocks[a].restore_backup(e.fblock_state[a]);
        fblock_index = e.fblock_index;
        amount_blockdata  = e.amount_blockdata;
        amount_fblockdata = e.amount_fblockdata;
    }

    /****************************************************/
    /* Block scheduler. It stores the scattered requests *
     * to read files and write data, and then does them  *
     * later together.                                   *
     *****************************************************/
    
    struct mkcromfs_blockify_schedule
    {
        /* This structures stores a request to blockify()
         * data from given source.
         * The blocks will be written into the given target.
         */
        /* The source of data may either be a vector,
         * or a filename.
         */
        mkcromfs_blockify_schedule(datasource_t* src, SchedulerDataClass dc)
            : source(src), target_vec(0), target_offs(0), dataclass(dc)
        {
        }
        
        void RepointAsRawInode
            (std::vector<unsigned char>& target,
             uint_fast64_t offset_inode)
        {
            // Change the schedule type as a reference to the numbered inode in inotab
            target_vec    = &target;
            target_offs   = offset_inode + 0x18; // address of blocklist in raw inode.
        }
        
        datasource_t* GetDataSource() const
        {
            return source;
        }
        
        unsigned char* GetBlockTarget() const
        {
            if(!target_vec)
            {
                fprintf(stderr, "Internal error: Blockify schedule %s has not been targeted!\n",
                    GetName().c_str());
                return 0;
            }
            return &(*target_vec)[target_offs];
        }
        
        const std::string GetName() const
        {
            return source->getname();
        }
        
        bool operator< (const mkcromfs_blockify_schedule& b) const
        {
            /* For some reason, even this seems to worsen the compression. */
            
            /* But it's added here anyway, because it will
             * probably make directory access times a little better.
             */
            if((int)dataclass != (int)(b.dataclass)) 
                return ((int)dataclass < (int)(b.dataclass));
            
            //return GetBlockTarget() < b.GetBlockTarget();
            
            /* So, no sort rule. */
            return false;
        }
        
    private:
        autoptr<datasource_t> source;
        
        std::vector<unsigned char>* target_vec;
        uint_fast64_t               target_offs;
        SchedulerDataClass          dataclass;
    };

    class schedule_t
    {
    private:
        cromfs& cromfs_obj;
        
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
            std::vector<unsigned char> data;
            
            /* needle is behind an autoptr, so that it doesn't always
             * need to be initialized. */
            autoptr<BoyerMooreNeedle> needle;
            uint_fast64_t offset;
            crc32_t crc;
            unsigned char* target; /* where to put blocknum */
            
            float badness; // for sorting
            
            bool already_tested_full_match;
            
            individual_order(const std::vector<unsigned char>& d,
                             unsigned char* t)
                : data(d),
                  crc(crc32_calc(&d[0], d.size())),
                  target(t), badness(0),
                  already_tested_full_match(false)
            {
            }
            
            void MakeNeedle()
            {
                needle = new BoyerMooreNeedle(data);
            }
            
            void Write(cromfs_blocknum_t num)
            {
                Wn(target, num, BLOCKNUM_SIZE_BYTES());
            }
            
            bool operator< (const individual_order& b) const
            {
                // primary sort key: badness.
                if(badness != b.badness) return badness < b.badness;
                
                // When they are equal, try to maintain old order.
                // This usually correlates with file position & file index:
                return target < b.target;
                //return offset < b.offset;
            }
            
            uint_fast32_t GetFblockTestingSize()
            {
                if(!already_tested_full_match)
                {
                    already_tested_full_match = true;
                    return FSIZE;
                }
                return data.size()*2;
            }
        };
        typedef std::list<individual_order> orderlist_t;
        orderlist_t blockify_orders;

    private:
        void AddOrder(const std::vector<unsigned char>& data,
                      uint_fast64_t offset,
                      unsigned char* target)
        {
            individual_order order(data, target);
            
            order.badness = offset; // a dummy sorting rule at first
            
            const ReusingPlan plan1 = cromfs_obj.CreateReusingPlan(order.data, order.crc);
            if(plan1)
            {
                order.Write(cromfs_obj.Execute(plan1));
                return; // Finished, don't need to postpone it.
            }

            //printf("Adding order crc %08X\n", order.crc);
            blockify_orders.push_back(order);
        }
    
        void HandleOrders(ssize_t max_remaining_orders = BlockifyAmount1)
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
                EvaluateBlockifyOrders();
            }
            // Now the least bad are handled first.
            blockify_orders.sort();
            
            /* number of orders might have changed, recheck it */
            num_handle = blockify_orders.size() - max_remaining_orders;
            
            for(orderlist_t::iterator j,i = blockify_orders.begin();
                num_handle > 0 && i != blockify_orders.end();
                i = j, --num_handle)
            {
                j = i; ++j;
                individual_order& order = *i;

                // Find the fblock that contains this given data, or if that's
                // not possible, find out which fblock to append to, or whether
                // to create a new fblock.
                const ReusingPlan plan1
                    = cromfs_obj.CreateReusingPlan(order.data, order.crc);
                if(plan1)
                {
                    /*
                    std::printf("Plan: %u,crc %08X,badness(%g) - ",
                        0,
                        order.crc,
                        order.badness);
                    */
                    order.Write(cromfs_obj.Execute(plan1));
                }
                else
                {
                    if(!order.needle) order.MakeNeedle();
                    
                    const WritePlan plan2 = cromfs_obj.CreateWritePlan(*order.needle, order.crc,
                        order.GetFblockTestingSize());
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
                    order.Write(cromfs_obj.Execute(plan2));
                }
                
                blockify_orders.erase(i);
                
                if(TryOptimalOrganization >= 2) goto ReEvaluate;
            }

            for(orderlist_t::iterator i = blockify_orders.begin();
                i != blockify_orders.end();
                ++i)
            {
                i->already_tested_full_match = false;
            }
        }
        
        void EvaluateBlockifyOrders()
        {
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
                    = cromfs_obj.CreateReusingPlan(order.data, order.crc);
                if(plan1)
                {
                    /* Surprise, it matched. Handle it immediately. */
                    order.Write(cromfs_obj.Execute(plan1));
                    blockify_orders.erase(i);
                    continue;
                }

                /* Check how well this order would be placed */
                if(!order.needle) order.MakeNeedle();
                const WritePlan plan2 = cromfs_obj.CreateWritePlan(*order.needle, order.crc,
                    order.GetFblockTestingSize());
                const AppendInfo& appended = plan2.appended;
                uint_fast32_t size_added   = appended.AppendedSize - appended.OldSize;
                uint_fast32_t overlap_size = order.data.size() - size_added;
                if(!size_added)
                {
                    /* It's a full overlap. Handle it immediately. */
                    order.Write(cromfs_obj.Execute(plan2));
                    blockify_orders.erase(i);
                    continue;
                }
                
                order.badness = -overlap_size;
            }
            
            /* Algorithm 2:
             *   Sort them in optimalness order. Most optimal first.
             *   Optimalness = - (total size if this block goes first,
             *                    and all of the remaining blocks go next )
             */
            if(TryOptimalOrganization >= 2)
            {
                for(orderlist_t::iterator
                    j = blockify_orders.begin();
                    j != blockify_orders.end();
                    ++j)
                {
                    const WritePlan plan = cromfs_obj.CreateWritePlan(*j->needle, j->crc,
                        j->GetFblockTestingSize());
                    
                    uint_fast32_t size_added_here
                        = plan.appended.AppendedSize - plan.appended.OldSize;
                    
                    cromfs::SituationBackup backup;
                    cromfs_obj.CreateBackup(backup);
                    
                    std::printf("> false write- ");
                    cromfs_obj.Execute(plan, false);

                    uint_fast64_t size_added_sub = 0, size_added_count = 0;
                    for(orderlist_t::iterator
                        i = blockify_orders.begin();
                        i != blockify_orders.end();
                        ++i)
                    {
                        if(i == j) continue;
                        const WritePlan plan2 =
                            cromfs_obj.CreateWritePlan(*i->needle, i->crc,
                                i->GetFblockTestingSize());
                        const AppendInfo& appended = plan2.appended;
                        size_added_sub += appended.AppendedSize - appended.OldSize;
                        ++size_added_count;
                    }
                    
                    j->badness = size_added_here;
                    if(size_added_count)
                        j->badness += size_added_sub / (double)size_added_count;
                    
                    std::printf(">> badness %g\n", j->badness);
                    
                    cromfs_obj.RestoreBackup(backup);
                }
            }
        }
    private:
        std::vector<mkcromfs_blockify_schedule> schedule;
    public:
        schedule_t(cromfs& c) : cromfs_obj(c)
        {
        }
        
        mkcromfs_blockify_schedule* Add(datasource_t* source, SchedulerDataClass dataclass)
        {
            schedule.push_back(mkcromfs_blockify_schedule(source, dataclass));
            return &*schedule.rbegin();
        }
        
        void DisplayProgress(uint_fast64_t pos, uint_fast64_t max,
                             uint_fast64_t bpos, uint_fast64_t bmax) const
        {
            char Buf[512];
            std::sprintf(Buf, "Blockifying: %5.1f%% (%llu/%llu) -- %llu blocks produced",
                pos
                * 100.0
                / max,
                (unsigned long long)bpos,
                (unsigned long long)bmax,
                (unsigned long long)cromfs_obj.blocks.size());
            if(DisplayBlockSelections)
                std::printf("%s\n", Buf);
            else
                std::printf("%s\r", Buf);
            /*
            std::printf("\33]2;mkcromfs: %s\7\r", Buf);
            std::fflush(stdout);
            */
        }
        
        void Flush()
        {
            std::stable_sort(schedule.begin(), schedule.end());
            
            uint_fast64_t total_size = 0, blocks_total = 0;
            uint_fast64_t total_done = 0, blocks_done  = 0;
            for(unsigned a=0; a<schedule.size(); ++a)
            {
                uint_fast64_t size = schedule[a].GetDataSource()->size();
                total_size   += size;
                blocks_total += (size + BSIZE-1) / BSIZE;
            }
            
            for(unsigned a=0; a<schedule.size(); ++a)
            {
                mkcromfs_blockify_schedule& s = schedule[a];
                
                datasource_t* source  = s.GetDataSource();
                unsigned char* target = s.GetBlockTarget();
                uint_fast64_t nbytes  = source->size();

                if(DisplayBlockSelections)
                    std::printf("%s\n", source->getname().c_str());
                
                ssize_t HandlingCounter = BlockifyAmount2;
                
                source->open();
                uint_fast64_t offset = 0;
                while(nbytes > 0)
                {
                    DisplayProgress(total_done, total_size, blocks_done, blocks_total);

                    uint_fast64_t eat = std::min((uint_fast64_t)BSIZE, nbytes);
                    
                    AddOrder(source->read(eat), offset, target);
                    
                    nbytes -= eat;
                    offset += eat;
                    target += BLOCKNUM_SIZE_BYTES(); // where pointer will be written to.
                    total_done += eat;
                    ++blocks_done;
                    
                    if(--HandlingCounter <= 0)
                        { HandlingCounter = BlockifyAmount2;
                          HandleOrders(); }
                }
                source->close();
                HandleOrders();
            }
            schedule.clear();
            
            DisplayProgress(total_done, total_size, blocks_done, blocks_total);
            
            HandleOrders(0);
        }
    } scheduler;
    
    mkcromfs_blockify_schedule* ScheduleBlockify(
        datasource_t* source,
        cromfs_inode_internal& inode,
        SchedulerDataClass dataclass)
    {
        //fprintf(stderr, "Writing bytesize %llu (%s)\n", source->size(), source->getname().c_str());
        inode.bytesize  = source->size();
        
        inode.blocklist.resize( (inode.bytesize + BSIZE - 1) / BSIZE );
        
        return scheduler.Add(source, dataclass);
    }
    
    void FlushBlockifyRequests()
    {
        scheduler.Flush();
    }
    
    /***********************************/
    /* Filesystem traversal functions. *
     ***********************************/

    typedef std::map<hardlinkdata, cromfs_inodenum_t> hardlinkmap_t;
    hardlinkmap_t hardlink_map;

    
    struct direntry
    {
        std::string name;
        struct stat64 st;
        char sortkey;
        
        bool operator< (const direntry& b) const
            { if(sortkey != b.sortkey) return sortkey < b.sortkey;
              return name < b.name;
            }
    };
    cromfs_dirinfo WalkDir(const std::string& path)
    {
        cromfs_dirinfo dirinfo;
        
        DIR* dir = opendir(path.c_str());
        if(!dir) { std::perror(path.c_str()); return dirinfo; }
        
        std::vector<direntry> entries;
        while(dirent* dent = readdir(dir))
        {
            const std::string entname = dent->d_name;
            if(entname == "." || entname == "..") continue;
            
            direntry ent;
            struct stat64& st = ent.st;
            
            const std::string pathname = path + "/" + entname;
            if(!MatchFile(pathname)) continue;

            if( (FollowSymlinks ? stat64 : lstat64) (pathname.c_str(), &st) < 0)
            {
                std::perror(pathname.c_str());
                continue;
            }
            
            ent.name = entname;
            if(S_ISLNK(st.st_mode)) ent.sortkey = DirParseOrder.Link;
            else if(S_ISDIR(st.st_mode)) ent.sortkey = DirParseOrder.Dir;
            else ent.sortkey = DirParseOrder.Other;
            entries.push_back(ent);
        }
        closedir(dir);
        
        std::sort(entries.begin(), entries.end());
        
        for(unsigned a=0; a<entries.size(); ++a)
        {
#if 0 /* PROFILING TEST */
            if(fblocks.size() >= 4)
            {
                break;
            }
#endif
            const std::string& entname = entries[a].name;
            const std::string pathname = path + "/" + entname;
            const struct stat64& st = entries[a].st;
            
            if(DisplayFiles)
            {
                std::printf("%s ...\n", pathname.c_str());
            }
            
            const hardlinkdata hardlink(st.st_dev, st.st_ino);
            {hardlinkmap_t::const_iterator i = hardlink_map.find(hardlink);
            if(i != hardlink_map.end())
            {
                /* A hardlink was found! */
                const cromfs_inodenum_t inonum = i->second;
                
                if(DisplayFiles)
                {
                    std::printf("- reusing inode %ld (hardlink)\n", (long)inonum);
                }
                
                /* Reuse the same inode number. */
                dirinfo[entname] = inonum;
                
                increment_inode_linkcount(inonum);
                continue;
            }}

            /* Not found, create new inode */
            cromfs_inode_internal inode;
            inode.mode     = st.st_mode;
            inode.time     = st.st_mtime;
            //Link count starts from 1. Don't copy it from host filesystem.
            inode.links    = 1;
            inode.bytesize = 0;
            inode.rdev     = 0;
            inode.uid      = st.st_uid;
            inode.gid      = st.st_gid;
            
            mkcromfs_blockify_schedule* schedule = 0;
            
            if(S_ISDIR(st.st_mode))
            {
                cromfs_dirinfo dirinfo = WalkDir(pathname);
                
                /* For directories, the number of links is
                 * the number of entries in the directory.
                 */
                inode.links = dirinfo.size();
                
                schedule = ScheduleBlockify(
                    new datasource_vector(encode_directory(dirinfo), pathname),
                    inode,
                    DataClassOrder.Directory);

                bytes_of_files += inode.bytesize;
            }
            else if(S_ISLNK(st.st_mode))
            {
                std::vector<unsigned char> Buf(4096);
                int res = readlink(pathname.c_str(), (char*)&Buf[0], Buf.size());
                if(res < 0) { std::perror(pathname.c_str()); continue; }
                Buf.resize(res);
                
                schedule = ScheduleBlockify(
                    new datasource_vector(Buf, pathname+" (link target)"),
                    inode,
                    DataClassOrder.Symlink);

                bytes_of_files += inode.bytesize;
            }
            else if(S_ISREG(st.st_mode))
            {
                schedule = ScheduleBlockify(
                    new datasource_file_name(pathname),
                    inode,
                    DataClassOrder.File);
                
                bytes_of_files += inode.bytesize;
            }
            else if(S_ISCHR(st.st_mode) || S_ISBLK(st.st_mode))
            {
                inode.rdev = st.st_rdev;
            }
            
            cromfs_inodenum_t inonum = create_inode(inode);
            
            if(schedule)
            {
                schedule->RepointAsRawInode(inotab, GetInodeOffset(inonum));
            }
            
            dirinfo[entname] = inonum;
            
            hardlink_map[hardlink] = inonum;
        }
        
        std::fflush(stdout);
        
        return dirinfo;
    }

    /********************************************/
    /* Methods for accessing / creating inodes. */
    /********************************************/

    static unsigned GetInodeSize(const cromfs_inode_internal& inode)
    {
        unsigned result = 0x18;
        
        result += inode.blocklist.size() * BLOCKNUM_SIZE_BYTES();
        
        // Round up to be evenly divisible by 4.
        result = (result + 3) & ~3;
        
        return result;
    }

    static uint_fast64_t GetInodeOffset(cromfs_inodenum_t inonum)
    {
        /* Returns a byte offset into inotab when given an inode number. */
        return (inonum-2)*4;
    }
    
    const std::vector<unsigned char> encode_inode(const cromfs_inode_internal& inode)
    {
        std::vector<unsigned char> result(GetInodeSize(inode));
        put_inode(&result[0], inode);
        return result;
    }
    
    void put_inode(unsigned char* inodata,
                   const cromfs_inode_internal& inode,
                   bool ignore_blocks = false)
    {
        uint_fast32_t rdev_links = inode.links;
        if(S_ISCHR(inode.mode) || S_ISBLK(inode.mode)) rdev_links = inode.rdev;
    
        W32(&inodata[0x00], inode.mode);
        W32(&inodata[0x04], inode.time);
        W32(&inodata[0x08], rdev_links);
        W16(&inodata[0x0C], inode.uid);
        W16(&inodata[0x0E], inode.gid);
        W64(&inodata[0x10], inode.bytesize);
        
        if(ignore_blocks) return;
        
        /* Endianess safe. */
        
        const unsigned b = BLOCKNUM_SIZE_BYTES();
        for(unsigned a=0; a<inode.blocklist.size(); ++a)
            Wn(&inodata[0x18+a*b], inode.blocklist[a], b);
    }
    
    void increment_inode_linkcount(cromfs_inodenum_t inonum)
    {
        uint_fast64_t pos = GetInodeOffset(inonum);
        unsigned char* inodata = &inotab[pos];
        
        uint_fast32_t mode  = R32(&inodata[0x00]);
        if(S_ISCHR(mode) || S_ISBLK(mode))
        {
            /* no links value on these inode types */
            return;
        }
        
        uint_fast32_t links = R32(&inodata[0x08]);
        ++links;
        W32(&inodata[0x08], links);
    }
    
    const cromfs_inodenum_t
        create_inode(const cromfs_inode_internal& inode)
    {
        /* Where to write = end of inode table */
        uint_fast64_t pos = inotab.size();
        
        /* How many bytes does this inode add? */
        size_t addsize = GetInodeSize(inode);
        
        /* Allocate that */
        inotab.resize(inotab.size() + addsize);

        /* This is the formula for making inode number from inode address */
        cromfs_inodenum_t inonum = (pos/4)+2;
        unsigned char* inodata = &inotab[pos];
        put_inode(inodata, inode, false);
        
        return inonum;
    }
    
    const std::vector<unsigned char>
        encode_directory(const cromfs_dirinfo& dir) const
    {
        std::vector<unsigned char> result(4 + 4*dir.size()); // buffer for pointers
        std::vector<unsigned char> entries;                  // buffer for names
        entries.reserve(dir.size() * (8 + 10)); // 10 = guestimate of average fn length
        
        W32(&result[0], dir.size());
        
        unsigned entrytableoffs = result.size();
        unsigned entryoffs = 0;
        
        unsigned diroffset=0;
        for(cromfs_dirinfo::const_iterator i = dir.begin(); i != dir.end(); ++i)
        {
            const std::string&     name = i->first;
            const cromfs_inodenum_t ino = i->second;
            
            W32(&result[4 + diroffset*4], entrytableoffs + entryoffs);
            
            entries.resize(entryoffs + 8 + name.size() + 1);
            
            W64(&entries[entryoffs], ino);
            std::memcpy(&entries[entryoffs+8], name.c_str(), name.size()+1);
            
            entryoffs = entries.size();
            ++diroffset;
        }
        // append the name buffer to the pointer buffer.
        result.insert(result.end(), entries.begin(), entries.end());
        return result;
    }

private:
    void UnmapOneRandomlyButNot(cromfs_fblocknum_t forbid)
    {
        //return;
        static cromfs_fblocknum_t counter = 0;
        if(counter < fblocks.size() && counter != forbid)
            fblocks[counter].Unmap();
        if(!fblocks.empty())
            counter = (counter+1) % fblocks.size();
    }

    void CompressOneRandomlyButNot(cromfs_fblocknum_t forbid)
    {
        static unsigned counter = RandomCompressPeriod;
        if(!counter) counter = RandomCompressPeriod; else { --counter; return; }
        
        /* postpone it if there are no fblocks */
        if(fblocks.empty()) { counter=0; return; }
        
        const cromfs_fblocknum_t c = std::rand() % fblocks.size();
        
        /* postpone it if we hit the landmine */
        if(c == forbid) { counter=0; return; }
        
        mkcromfs_fblock& fblock = fblocks[c];

        /* postpone it if this fblock doesn't need compressing */
        if(!fblock.is_uncompressed()) { counter=0; return; }
        
        if(fblock.is_uncompressed())
        {
            fblock.put_compressed(LZMACompress(fblock.get_raw()));
        }
    }
    
private:
    cromfs(cromfs&);
    void operator=(const cromfs&);
};

/*
static void TestCompression()
{
    std::vector<unsigned char> buf;
    for(unsigned a=0; a<40; ++a)
        buf.push_back("aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa"[a]);
    buf = LZMACompress(buf);
    std::fprintf(stderr, "buf size now = %u\n", buf.size());
    for(unsigned a=0; a<buf.size(); ++a) std::fprintf(stderr, " %02X", buf[a]);
    std::fprintf(stderr, "\n");

    buf = LZMADeCompress(buf);
    std::fprintf(stderr, "buf size now = %u\n", buf.size());

    for(unsigned a=0; a<buf.size(); ++a) std::fprintf(stderr, " %02X", buf[a]);
    std::fprintf(stderr, "\n");
}
*/

class EstimateSpaceNeededFor
{
private:
    std::set<hardlinkdata> hardlink_set;

    bool check_hardlink_file(dev_t dev, ino_t ino)
    {
        hardlinkdata d(dev, ino);
        std::set<hardlinkdata>::const_iterator i = hardlink_set.find(d);
        if(i == hardlink_set.end()) { hardlink_set.insert(d); return false; }
        return true;
    }
public:
    uint_fast64_t num_blocks;
    uint_fast64_t num_file_bytes;
    uint_fast64_t num_inodes;
    
    EstimateSpaceNeededFor(const std::string& path):
        hardlink_set(), num_blocks(0),
        num_file_bytes(0), num_inodes(0)
    {
        Handle(path);
    }
private:
    void Handle(const std::string& path)
    {
        DIR* dir = opendir(path.c_str());
        if(!dir) return;
        
        std::vector<std::string> dirs;
        while(dirent* ent = readdir(dir))
        {
            const std::string entname = ent->d_name;
            if(entname == "." || entname == "..") continue;
            const std::string pathname = path + "/" + entname;
            if(!MatchFile(pathname)) continue;
            struct stat64 st;
            if( (FollowSymlinks ? stat64 : lstat64) (pathname.c_str(), &st) < 0) continue;
            
            /* Count the size of the directory entry */
            num_file_bytes += 8 + entname.size() + 1;
            
            if(check_hardlink_file(st.st_dev, st.st_ino))
            {
                continue; // nothing more to do for this entry
            }
            
            if(S_ISDIR(st.st_mode)) { dirs.push_back(pathname); continue; }
            
            /* Count the size of the content */
            uint_fast64_t file_size   = st.st_size;
            uint_fast64_t file_blocks = (file_size + BSIZE-1) / BSIZE;
            
            num_file_bytes += file_size;
            num_blocks     += file_blocks;
            num_inodes     += 1;
        }
        closedir(dir);
        
        for(unsigned a=0; a<dirs.size(); ++a)
            Handle(dirs[a]);
    }
};

class CheckSomeDefaultOptions
{
public:
    CheckSomeDefaultOptions(const std::string& rootpath)
    {
        EstimateSpaceNeededFor estimate(rootpath);
    
        if(MayAutochooseBlocknumSize)
        {
            if(estimate.num_blocks < 0x10000UL)
            {
                std::printf(
                    "mkcromfs: Automatically enabling --16bitblocknums because it seems possible for this filesystem.\n");
                storage_opts |= CROMFS_OPT_16BIT_BLOCKNUMS;
            }
            else if(estimate.num_blocks < 0x1000000UL)
            {
                std::printf(
                    "mkcromfs: Automatically enabling --24bitblocknums because it seems possible for this filesystem.\n");
                storage_opts |= CROMFS_OPT_24BIT_BLOCKNUMS;
            }
        }

        if(MaySuggestDecompression)
        {
            uint_fast64_t space_needed =
                estimate.num_inodes * 0x18
              + estimate.num_blocks * BLOCKNUM_SIZE_BYTES()
              + estimate.num_blocks * ((storage_opts & CROMFS_OPT_PACKED_BLOCKS) ? 4 : 8)
              + estimate.num_file_bytes;
            
            std::string TempDir = GetTempDir();
            struct statfs64 stats;
            if(statfs64(TempDir.c_str(), &stats) != 0)
            {
                perror(TempDir.c_str()); // not a severe error though
            }
            else
            {
                uint_fast64_t space_have   = stats.f_bfree * stats.f_bsize;
                
                if(space_needed >= space_have) goto NotEnabled;

                std::printf(
                    "Good news! Your tempdir, [1m%s[0m, has [1m%s[0m free.\n"
                    "mkcromfs estimates that if the [1m-e -r100000[0m options were enabled,\n"
                    "it would require about [1m%s[0m of temporary disk space.\n"
                    "This would be likely much faster than the default method.\n"
                    "The default method requires less temporary space, but is slower.\n",
                    TempDir.c_str(),
                    ReportSize(space_have).c_str(),
                    ReportSize(space_needed).c_str());
                
                if(space_needed < space_have / 20)
                {
                    std::printf(
                        "Actually, the ratio is so small that I'll just enable\n"
                        "the mode without even asking. You can suppress this behavior\n"
                        "by manually specifying one of the -e, -r or -q options.\n");
                    goto Enabled;
                }
                for(;;)
                {
                    std::printf(
                        "Do you want to enable the 'decompresslookups' mode? (Y/N) ");
                    std::fflush(stdout);
                    char Buf[512];
                    if(!std::fgets(Buf, sizeof(Buf), stdin))
                    {
                        std::printf("\nEOF from stdin, assuming the answer is \"no\".\n");
                        goto NotEnabled;
                    }
                    if(Buf[0] == 'y' || Buf[0] == 'Y') goto Enabled;
                    if(Buf[0] == 'n' || Buf[0] == 'N') goto NotEnabled;
                    std::printf("Invalid input, please try again...\n");
                }
            Enabled:
                std::printf("Activating commandline options: -e -r100000%s\n",
                    MaxFblockCountForBruteForce ? "" : " -c2");
                DecompressWhenLookup = true;
                RandomCompressPeriod = 100000;
                if(!MaxFblockCountForBruteForce) MaxFblockCountForBruteForce = 2;
            NotEnabled: ;
            }
        }
    }
};


static cromfs* cleanup_cromfs_handle = 0;
static void CleanupTempsExit(int signo)
{
    /* Note: This is not valid in the strictest sense of the C++ standard.
     * A signal handler may only use language features present in the C language.
     * However, in practise it works just fine and nobody complains.
     */
    std::printf("\nTermination signalled, cleaning up temporaries\n");
    if(cleanup_cromfs_handle) cleanup_cromfs_handle->~cromfs();
    signal(signo, SIG_DFL);
    raise(signo); // reraise the signal
}

int main(int argc, char** argv)
{
    std::string path  = ".";
    std::string outfn = "cromfs.bin";
    
    unsigned AutoIndexRatio = 16;

    signal(SIGINT, CleanupTempsExit);
    signal(SIGTERM, CleanupTempsExit);
    signal(SIGHUP, CleanupTempsExit);
    signal(SIGQUIT, CleanupTempsExit);
    
    for(;;)
    {
        int option_index = 0;
        static struct option long_options[] =
        {
            {"help",        0, 0,'h'},
            {"version",     0, 0,'V'},
            {"fsize",       1, 0,'f'},
            {"bsize",       1, 0,'b'},
            {"decompresslookups",
                            0, 0,'e'},
            {"randomcompressperiod",
                            1, 0,'r'},
            {"minfreespace",
                            1, 0,'s'},
            {"autoindexratio",
                            1, 0,'a'},
            {"bruteforcelimit",
                            1, 0,'c'},
            {"followsymlinks", 0,0,'l'},
            {"exclude",     1, 0,'x'},
            {"exclude-from",1, 0,'X'},
            {"quiet",       0, 0,'q'},
            {"verbose",     0, 0,'v'},
            {"sparse",      1, 0,'S'},
            {"blockifybufferlength",    1,0,3001},
            {"blockifybufferincrement", 1,0,3002},
            {"blockifyoptimizemethod",  1,0,3003},
            {"blockifyoptimisemethod",  1,0,3003},
            {"32bitblocknums",          0,0,'4'},
            {"24bitblocknums",          0,0,'3'},
            {"16bitblocknums",          0,0,'2'},
            {"nopackedblocks",          0,0,6001},
            {"lzmafastbytes",           1,0,4001},
            {"lzmabits",                1,0,4002},
            {"blockifyorder",           1,0,5001},
            {"dirparseorder",           1,0,5002},
            {"bwt",                     0,0,2001},
            {"mtf",                     0,0,2002},
            {0,0,0,0}
        };
        int c = getopt_long(argc, argv, "hVvf:b:er:s:a:c:qx:X:lS:432", long_options, &option_index);
        if(c==-1) break;
        switch(c)
        {
            case 'V':
            {
                std::printf("%s\n", VERSION);
                return 0;
            }
            case 'h':
            {
                std::printf(
                    "mkcromfs v"VERSION" - Copyright (C) 1992,2007 Bisqwit (http://iki.fi/bisqwit/)\n"
                    "\n"
                    "Usage: mkcromfs [<options>] <input_path> <target_image>\n"
                    "\n"
                    "Basic options:\n"
                    " --help, -h         This help\n"
                    " --version, -V      Displays version information\n"
                    " --quiet, -q\n"
                    "     -q supresses the detailed information outputting while compressing.\n"
                    "     -qq supresses also the listing of the filenames.\n"
                    "     -qqq supresses also the summary and progress at the last phase.\n"
                    " --verbose, -v\n   Opposite meaning of --quiet, but not exactly\n"
                    "     -v makes auto/full LZMA compression a bit more verbose\n"
                    "     -vv makes auto/full LZMA compression a lot more verbose\n"
                    "\n"
                    "File selection parameters:\n"
                    " --exclude, -x <pattern>\n"
                    "     Exclude files matching <pattern> from the archive\n"
                    " --exclude-from, -X <file>\n"
                    "     Exclude files matching the patterns in <file>\n"
                    " --followsymlinks, -l\n"
                    "     Follow symlinks instead of storing them (same the referred contents)\n"
                    "\n"
                    "Operation parameters:\n"
                    " --decompresslookups, -e\n"
                    "     Save decompressed data into a temporary file when it needs to be\n"
                    "     looked up for identical block verification. Speeds up mkcromfs\n"
                    "     somewhat, but may require as much free diskspace as the source\n"
                    "     files are together.\n"
                    " --randomcompressperiod, -r <value>\n"
                    "     Interval for randomly picking one fblock to compress. Default: 20\n"
                    "     The value has no effect on the compression ratio of the filesystem,\n"
                    "     but smaller values mean slower filesystem creation and bigger values\n"
                    "     mean more diskspace used by temporary files.\n"
                    "\n"
                    "Filesystem parameters:\n"
                    " --fsize, -f <size>\n"
                    "     Set the size of compressed data clusters. Default: 2097152\n"
                    "     Larger cluster size improves compression, but increases the memory\n"
                    "     usage during mount, and makes the filesystem a lot slower to generate.\n"
                    "     Should be set at least twice as large as bsize.\n"
                    " --bsize, -b <size>\n"
                    "     Set the size of file fragments. Default: 65536\n"
                    "     Smaller fragment size improves the merging of identical file content,\n"
                    "     but causes a larger block table to be generated, and slows down the\n"
                    "     creation of the filesystem.\n"
#if 0
                    " --sparse, -S <opts>\n"
                    "     Commaseparated list of items to store sparsely. -Sf = fblocks\n"
                    "     Use this option only when creating appendable filesystems.\n"
#endif
                    " --24bitblocknums, -3\n"
                    "     Tells mkcromfs to store block numbers in 3 bytes instead of 4.\n"
                    "     This will result in smaller inodes, potentially saving space\n"
                    "     if your filesystem contains a lot of files. However, it will\n"
                    "     also restrict the number of blocks to 16777216 instead of the\n"
                    "     default value of 4294967296.\n"
                    "     If your total size of files divided by the selected bsize\n"
                    "     approaches this limit, do not use this option.\n"
                    " --16bitblocknums, -2\n"
                    "     Tells mkcromfs to store block numbers in 2 bytes instead of 4.\n"
                    "     This will result in smaller inodes, potentially saving space\n"
                    "     if your filesystem contains a lot of files. However, it will\n"
                    "     also restrict the number of blocks to 65536 instead of the\n"
                    "     default value of 4294967296.\n"
                    "     If your total size of files divided by the selected bsize\n"
                    "     approaches this limit, do not use this option.\n"
                    " --32bitblocknums, -4\n"
                    "     By default, mkcromfs automatically chooses --16bitblocknums\n"
                    "     or --24bitblocknums if it detects that either one is possible.\n"
                    "     However, with this option you can force it to use 32-bit blocknums\n"
                    "     instead, overriding the automatical detection.\n"
                    " --nopackedblocks\n"
                    "     By default, mkcromfs stores blocks in 4 bytes instead of 8 bytes,\n"
                    "     if it is possible for the filesystem being created. However, you\n"
                    "     may use this option to disable it (for example, if you are creating\n"
                    "     a filesystem that may be write-extended).\n"
                    "     This option supersedes the old --packedblocks (-k) with opposite\n"
                    "     semantics.\n"
                    " --bwt\n"
                    "     Use BWT transform when compressing. Experimental.\n"
                    " --mtf\n"
                    "     Use MTF transform when compressing. Experimental.\n"
                    "\n"
                    "Compression algorithm parameters:\n"
                    " --minfreespace, -s <value>\n"
                    "     Minimum free space in a fblock to consider it a candidate. Default: 16\n"
                    "     The value should be smaller than bsize, or otherwise it works against\n"
                    "     the fsize setting by making the fsize impossible to reach.\n"
                    "     Note: The bruteforcelimit algorithm ignores minfreespace.\n"
                    " --autoindexratio, -a <value>\n"
                    "     Defines the ratio of indexes to blocks which to use when creating\n"
                    "     the filesystem. Default value: 16\n"
                    "     For example, if your bsize is 10000 and autoindexratio is 10,\n"
                    "     it means that for each 10000-byte block, 10 index entries will\n"
                    "     be created. Larger values help compression, but will use more RAM.\n"
                    "     The RAM required is approximately:\n"
                    "         (total_filesize * autoindexratio * N / bsize) bytes\n"
                    "     where N is around 16 on 32-bit systems, around 28 on 64-bit\n"
                    "     systems, plus memory allocation overhead.\n"
                    " --bruteforcelimit, -c <value>\n"
                    "     Set the maximum number of previous fblocks to search for\n"
                    "     overlapping content when deciding which fblock to append to.\n"
                    "     The default value, 0, means to do straight-forward selection\n"
                    "     based on the free space in the fblock.\n"
                    " --lzmafastbytes <value>\n"
                    "     Specifies the number of \"fast bytes\" in LZMA compression\n"
                    "     algorithm. Valid values are 5..273. Default is 273.\n"
                    " --lzmabits <pb>,<lp>,<lc>\n"
                    "     Sets the values for PosStateBits, LiteralPosStateBits\n"
                    "     and LiteralContextBits in LZMA properties.\n"
                    "      pb: Default value 0, allowed values 0..4\n"
                    "      lp: Default value 0, allowed values 0..4\n"
                    "      lc: Default value 1, allowed values 0..8\n"
                    "     Further documentation on these values is available in LZMA SDK.\n"
                    "     See file util/lzma/lzma.txt in the cromfs source distribution.\n"
                    "     Example: --lzmabits 0,0,3 (for text data)\n"
                    "     Alternatively, you can choose \"--lzmabits full\", which will\n"
                    "     try every possible option. Beware it will consume lots of time.\n"
                    "     \"--lzmabits auto\" is a lighter alternative to \"--lzmabits full\",\n"
                    "     and enabled by default.\n"
                    " --blockifybufferincrement <value>\n"
                    "     Number of blocks (of bsize) to handle simultaneously (default: 1)\n"
                    " --blockifybufferlength <value>\n"
                    "     Number of blocks to keep in a pending buffer (default: 64)\n"
                    "     Increasing this number will increase memory and CPU usage, and\n"
                    "     may sometimes yield a better compression, sometimes worse.\n"
                    "     Use 0 to disable.\n"
                    " --blockifyoptimizemethod <value>\n"
                    "     Selects method for selecting blocks to \"blockify\"\n"
                    "     0 (default) = straightforward selection\n"
                    "     1 = selects the pending block which gets largest overlap\n"
                    "     2 = selects the pending block which enables best compression for others\n"
                    "     This feature is under development. For now, 0 is usually best.\n"
                    " --blockifyorder <value>\n"
                    "     Specifies the priorities for blockifying different types of data\n"
                    "     Default: link=1,dir=2,file=3,inotab=4\n"
                    "     Changing it may affect compressibility.\n"
                    " --dirparseorder <value>\n"
                    "     Specifies the priorities for storing different types of elements\n"
                    "     in directories. Default:  link=2,dir=3,other=1\n"
                    "     Changing it may affect compressibility.\n"
                    "\n");
                return 0;
            }
            case 'f':
            {
                char* arg = optarg;
                long size = strtol(arg, &arg, 10);
                FSIZE = size;
                if(FSIZE < 64)
                {
                    std::fprintf(stderr, "mkcromfs: The minimum allowed fsize is 64. You gave %ld%s.\n", FSIZE, arg);
                    return -1;
                }
                if(FSIZE > 0x7FFFFFFE)
                {
                    std::fprintf(stderr, "mkcromfs: The maximum allowed fsize is 0x7FFFFFFE. You gave 0x%lX%s.\n", FSIZE, arg);
                    return -1;
                }
                break;
            }
            case 'b':
            {
                char* arg = optarg;
                long size = strtol(arg, &arg, 10);
                BSIZE = size;
                if(BSIZE < 8)
                {
                    std::fprintf(stderr, "mkcromfs: The minimum allowed bsize is 8. You gave %ld%s.\n", BSIZE, arg);
                    return -1;
                }
                break;
            }
            case 'e':
            {
                DecompressWhenLookup = true;
                MaySuggestDecompression = false;
                break;
            }
            case 'l':
            {
                FollowSymlinks = true;
                break;
            }
            case 'r':
            {
                char* arg = optarg;
                long val = strtol(arg, &arg, 10);
                if(val < 1)
                {
                    std::fprintf(stderr, "mkcromfs: The minimum allowed randomcompressperiod is 1. You gave %ld%s.\n", val, arg);
                    return -1;
                }
                RandomCompressPeriod = val;
                MaySuggestDecompression = false;
                break;
            }
            case 'a':
            {
                char* arg = optarg;
                long val = strtol(arg, &arg, 10);
                if(val < 1)
                {
                    std::fprintf(stderr, "mkcromfs: The minimum allowed autoindexratio is 1. You gave %ld%s.\n", val, arg);
                    return -1;
                }
                AutoIndexRatio = val;
                break;
            }
            case 's':
            {
                char* arg = optarg;
                long val = strtol(arg, &arg, 10);
                if(val < 0)
                {
                    std::fprintf(stderr, "mkcromfs: The minimum allowed minfreespace is 0. You gave %ld%s.\n", val, arg);
                    return -1;
                }
                MinimumFreeSpace = val;
                break;
            }
            case 'c':
            {
                char* arg = optarg;
                long val = strtol(arg, &arg, 10);
                if(val < 0)
                {
                    std::fprintf(stderr, "mkcromfs: The minimum allowed bruteforcelimit is 0. You gave %ld%s.\n", val, arg);
                    return -1;
                }
                if(val < 1) val = 1;
                MaxFblockCountForBruteForce = val;
                break;
            }
            case 'q':
            {
                if(DisplayBlockSelections) DisplayBlockSelections = false;
                else if(DisplayFiles) DisplayFiles = false;
                else if(DisplayEndProcess) DisplayEndProcess = false;
                else
                    std::fprintf(stderr, "mkcromfs: -qqqq not known, -qqq is the maximum.\n");
                MaySuggestDecompression = false;
                break;
            }
            case 'v':
            {
                ++LZMA_verbose;
                break;
            }
            case 'x':
            {
                AddFilePattern(exclude_files, optarg);
                break;
            }
            case 'X':
            {
                AddFilePatternsFrom(exclude_files, optarg);
                break;
            }
            case 'S':
            {
                for(char* arg=optarg;;)
                {
                    while(*arg==' ')++arg;
                    if(!*arg) break;
                    char* comma = strchr(arg, ',');
                    if(!comma) comma = strchr(arg, '\0');
                    *comma = '\0';
                    int len = strlen(arg);
                    if(len <= 7 && !strncmp("fblocks",arg,len))
                        storage_opts |= CROMFS_OPT_SPARSE_FBLOCKS;
                    else
                    {
                        std::fprintf(stderr, "mkcromfs: Unknown option to -p (%s). See `mkcromfs --help'\n",
                            arg);
                        return -1;
                    }
                    if(!*comma) break;
                    arg=comma+1;
                }
                break;
            }
            case '4':
            {
                if(storage_opts & (CROMFS_OPT_16BIT_BLOCKNUMS | CROMFS_OPT_24BIT_BLOCKNUMS))
                {
                    std::fprintf(stderr, "mkcromfs: You can only select one of the -2, -3 and -4 options.\n");
                    return -1;
                }
                MayAutochooseBlocknumSize = false;
                break;
            }
            case '3':
            {
                storage_opts |= CROMFS_OPT_24BIT_BLOCKNUMS;
                if(!MayAutochooseBlocknumSize || (storage_opts & CROMFS_OPT_16BIT_BLOCKNUMS))
                {
                    std::fprintf(stderr, "mkcromfs: You can only select one of the -2, -3 and -4 options.\n");
                    return -1;
                }
                MayAutochooseBlocknumSize = false;
                break;
            }
            case '2':
            {
                storage_opts |= CROMFS_OPT_16BIT_BLOCKNUMS;
                if(!MayAutochooseBlocknumSize || (storage_opts & CROMFS_OPT_24BIT_BLOCKNUMS))
                {
                    std::fprintf(stderr, "mkcromfs: You can only select one of the -2, -3 and -4 options.\n");
                    return -1;
                }
                MayAutochooseBlocknumSize = false;
                break;
            }
            case 2001: // bwt
            {
                storage_opts |= CROMFS_OPT_USE_BWT;
                break;
            }
            case 2002: // mtf
            {
                storage_opts |= CROMFS_OPT_USE_MTF;
                break;
            }
            case 6001: // nopackedblocks
            {
                MayPackBlocks = false;
                break;
            }
            case 4001: // lzmafastbytes
            {
                char* arg = optarg;
                long size = strtol(arg, &arg, 10);
                if(size < 5 || size > 273)
                {
                    std::fprintf(stderr, "mkcromfs: The number of \"fast bytes\" for LZMA may be 5..273. You gave %ld%s.\n", size,arg);
                    return -1;
                }
                LZMA_NumFastBytes = size;
                break;
            }
            case 4002: // lzmabits
            {
                unsigned arg_index = 0;
                for(char* arg=optarg;;)
                {
                    if(!arg_index && !strcmp(arg, "auto")) { LZMA_HeavyCompress=1; break; }
                    if(!arg_index && !strcmp(arg, "full")) { LZMA_HeavyCompress=2; break; }
                    LZMA_HeavyCompress=0;
                    while(*arg==' ')++arg;
                    if(!*arg) break;
                    char* comma = strchr(arg, ',');
                    if(!comma) comma = strchr(arg, '\0');
                    bool last_comma = !*comma;
                    *comma = '\0';
                    long value = strtol(arg, &arg, 10);
                    long max=4;
                    switch(arg_index)
                    {
                        case 0: // pb
                            LZMA_PosStateBits = value;
                            //fprintf(stderr, "pb=%ld\n", value);
                            break;
                        case 1: // lp
                            LZMA_LiteralPosStateBits = value;
                            //fprintf(stderr, "lp=%ld\n", value);
                            break;
                        case 2: // lc
                            LZMA_LiteralContextBits = value;
                            //fprintf(stderr, "lc=%ld\n", value);
                            max=8;
                            break;
                    }
                    if(value < 0 || value > max || arg_index > 2)
                    {
                        std::fprintf(stderr, "mkcromfs: Invalid value(s) for --lzmabits. See `mkcromfs --help'\n");
                        return -1;
                    }
                    if(last_comma) break;
                    arg=comma+1;
                    ++arg_index;
                }
                break;
            }
            case 3001: // blockifybufferlength
            {
                char* arg = optarg;
                long size = strtol(arg, &arg, 10);
                if(size < 0)
                {
                    std::fprintf(stderr, "mkcromfs: The parameter blockifybufferlength may not be negative. You gave %ld%s.\n", size,arg);
                    return -1;
                }
                BlockifyAmount1 = size;
                break;
            }
            case 3002: // blockifybufferincrement
            {
                char* arg = optarg;
                long size = strtol(arg, &arg, 10);
                if(size < 1)
                {
                    std::fprintf(stderr, "mkcromfs: The parameter blockifybufferincrement may not be less than 1. You gave %ld%s.\n", size,arg);
                    return -1;
                }
                BlockifyAmount2 = size;
                break;
            }
            case 3003: // blockifyoptimizemethod
            {
                char* arg = optarg;
                long size = strtol(arg, &arg, 10);
                if(size < 0 || size > 2)
                {
                    std::fprintf(stderr, "mkcromfs: Blockifyoptimizemethod may only be 0, 1 or 2. You gave %ld%s.\n", size,arg);
                    return -1;
                }
                TryOptimalOrganization = size;
                break;
            }
            case 5001: // blockifyorder
            case 5002: // dirparseorder
            {
                char *lnk =   c==5001 ? &DataClassOrder.Symlink : &DirParseOrder.Link
                   , *dir =   c==5001 ? &DataClassOrder.Directory : &DirParseOrder.Dir
                   , *file=   c==5001 ? &DataClassOrder.File : (char*)0
                   , *inotab= c==5001 ? &DataClassOrder.Inotab : (char*)0
                   , *other=  c==5001 ? (char*)0 : &DirParseOrder.Other;
                for(char* arg=optarg;;)
                {
                    char* value_target = 0;
                    
                    while(*arg==' ')++arg;
                    if(!*arg) break;
                    char* comma = strchr(arg, ',');
                    if(!comma) comma = strchr(arg, '\0');
                    bool last_comma = !*comma;
                    *comma = '\0';
                    
                    char* eqpos = strchr(arg, '=');
                    if(!eqpos)
                    {
                  invalid_param_5001:
                        std::fprintf(stderr, "mkcromfs: Invalid parameters (%s). See `mkcromfs --help'\n", arg);
                    }
                    else
                    {
                        *eqpos = '\0';
                        if(!strcmp("link", arg)) value_target=lnk;
                        else if(!strcmp("dir", arg)) value_target=dir;
                        else if(!strcmp("file", arg)) value_target=file;
                        else if(!strcmp("inotab", arg)) value_target=inotab;
                        else if(!strcmp("other", arg)) value_target=other;
                        if(!value_target) goto invalid_param_5001;
                        arg = eqpos+1;
                        long value = strtol(arg, &arg, 10);
                        *value_target = value;
                        if(value < 0 || value > 5)
                        {
                            std::fprintf(stderr, "mkcromfs: Invalid parameters (%ld%s). See `mkcromfs --help'\n",
                                value, arg);
                            return -1;
                        }
                    }
                    if(last_comma) break;
                    arg=comma+1;
                }
                break;
            }
        }
    }
    if(argc != optind+2)
    {
        std::fprintf(stderr, "mkcromfs: invalid parameters. See `mkcromfs --help'\n");
        return 1;
    }
    
    if(FSIZE < BSIZE)
    {
        std::fprintf(stderr,
            "mkcromfs: Error: Your fsize %ld is smaller than your bsize %ld.\n"
            "  Cannot comply.\n",
            (long)FSIZE, (long)BSIZE);
    }
    if((long)MinimumFreeSpace >= BSIZE)
    {
        std::fprintf(stderr,
            "mkcromfs: Warning: Your minfreespace %ld is quite high when compared to your\n"
            "  bsize %ld. Unless you rely on bruteforcelimit, it looks like a bad idea.\n",
            (long)MinimumFreeSpace, (long)BSIZE);
    }
    if((long)MinimumFreeSpace >= FSIZE)
    {
        std::fprintf(stderr,
            "mkcromfs: Error: Your minfreespace %ld is larger than your fsize %ld.\n"
            "  Cannot comply.\n",
            (long)MinimumFreeSpace, (long)FSIZE);
    }
    
    AutoIndexPeriod = std::max((long)AutoIndexRatio, (long)(BSIZE / AutoIndexRatio));
    
    if(AutoIndexPeriod < 1)
    {
        std::fprintf(stderr,
            "mkcromfs: Error: Your autoindexratio %ld is larger than your bsize %ld.\n"
            "  Cannot comply.\n", (long)AutoIndexRatio, BSIZE);
    }
    if(AutoIndexPeriod <= 4)
    {
        char Buf[256];
        if(AutoIndexPeriod == 1) std::sprintf(Buf, "for every possible byte");
        else std::sprintf(Buf, "every %u bytes", (unsigned)AutoIndexPeriod);
        
        std::fprintf(stderr,
            "mkcromfs: The autoindexratio you gave, %ld, means that a _severe_ amount\n"
            "  of memory will be used by mkcromfs. An index will be added %s.\n"
            "  Just thought I should warn you.\n",
            (long)AutoIndexRatio, Buf);
    }
    
    path  = argv[optind+0];
    outfn = argv[optind+1];
    
    int fd = open(outfn.c_str(), O_WRONLY | O_CREAT | O_LARGEFILE, 0644);
    if(fd < 0)
    {
        std::perror(outfn.c_str());
        return errno;
    }
    ftruncate64(fd, 0);
    
    (CheckSomeDefaultOptions(path));

    cromfs fs;
    cleanup_cromfs_handle = &fs;
    fs.WalkRootDir(path.c_str());
    
    if(DisplayEndProcess)
    {
        std::printf("Writing %s...\n", outfn.c_str());
    }
    
    //MaxSearchLength = FSIZE;
    
    ftruncate(fd, 0);
    fs.WriteTo(fd);
    close(fd);
    
    if(DisplayEndProcess)
    {
        std::printf("End\n");
    }
    
    return fs.GetExitStatus();
}
