#include "cromfs-blockfun.hh"
#include "lzma.hh"
#include "endian.hh"

#include "../cromfs-defs.hh"

/* Decode a block table.
 * Decompression is assumed to have already been done.
 * This is because decompression is done in an optimized
 * manner separately in cromfs.cc.
 */
const std::vector<cromfs_block_internal> DecodeBlockTable
    (const std::vector<unsigned char>& blktab_data,
     long FSIZE,
     uint_fast32_t storage_opts)
{
    std::vector<cromfs_block_internal> blktab;

    unsigned onesize = DATALOCATOR_SIZE_BYTES();
    if(storage_opts & CROMFS_OPT_PACKED_BLOCKS)
    {
        blktab.resize(blktab_data.size() / onesize);
        #pragma omp parallel for schedule(static)
        for(long a=0; a < (long)blktab.size(); ++a)
        {
            uint_fast32_t value = R32(&blktab_data[a*onesize]);
            uint_fast32_t fblocknum = value / FSIZE,
                          startoffs = value % FSIZE;

            //fprintf(stderr, "P block %u defined as %u:%u\n", a, (unsigned)fblocknum, (unsigned)startoffs);
            blktab[a].define(fblocknum, startoffs/*, CROMFS_BSIZE,CROMFS_FSIZE*/);
        }
    }
    else
    {
        blktab.resize(blktab_data.size() / onesize);
        #pragma omp parallel for schedule(static)
        for(long a=0; a < (long)blktab.size(); ++a)
        {
            uint_fast32_t fblocknum = R32(&blktab_data[a*onesize+0]),
                          startoffs = R32(&blktab_data[a*onesize+4]);
            //fprintf(stderr, "NP block %u defined as %u:%u\n", a, (unsigned)fblocknum, (unsigned)startoffs);
            blktab[a].define(fblocknum, startoffs/*, CROMFS_BSIZE,CROMFS_FSIZE*/);
        }
    }

    return blktab;
}
