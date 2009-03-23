#include "../cromfs-defs.hh"
#include <vector>

/* Encode + compress a block table. */
template<typename VecType>
const std::vector<unsigned char> CompressBlockTable
    (const VecType& blocks,
     uint_fast32_t storage_opts,
     long FSIZE,
     int heavy_option)
{
    unsigned onesize = DATALOCATOR_SIZE_BYTES();

    std::vector<unsigned char> raw_blktab(blocks.size() * onesize);
    if(storage_opts & CROMFS_OPT_PACKED_BLOCKS)
        for(unsigned a=0; a<blocks.size(); ++a)
        {
            uint_fast32_t fblocknum = blocks[a].fblocknum;
            uint_fast32_t startoffs = blocks[a].startoffs;

            //fprintf(stderr, "Writing P block %u = %u:%u\n", a,fblocknum,startoffs);

            W32(&raw_blktab[a*onesize], fblocknum * FSIZE + startoffs);
        }
    else
        for(unsigned a=0; a<blocks.size(); ++a)
        {
            uint_fast32_t fblocknum = blocks[a].fblocknum;
            uint_fast32_t startoffs = blocks[a].startoffs;

            //fprintf(stderr, "Writing NP block %u = %u:%u\n", a,fblocknum,startoffs);

            W32(&raw_blktab[a*onesize+0], fblocknum);
            W32(&raw_blktab[a*onesize+4], startoffs);
        }

    if(heavy_option == 2)
    {
        return LZMACompressHeavy(raw_blktab, "raw_blktab");
    }
    else
    {
        /* Make an educated guess of the optimal parameters for blocktab compression */
        const unsigned blktab_periodicity
            = (DATALOCATOR_SIZE_BYTES() == 4) ? 2 : 3;

        return LZMACompress(raw_blktab,
            blktab_periodicity,
            blktab_periodicity,
            0);
    }
}


/* Decode a block table.
 * Decompression is assumed to have already been done.
 * This is because decompression is done in an optimized
 * manner separately in cromfs.cc.
 */
const std::vector<cromfs_block_internal> DecodeBlockTable
    (const std::vector<unsigned char>&data,
     long FSIZE,
     uint_fast32_t storage_opts);
