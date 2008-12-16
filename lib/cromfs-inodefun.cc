#include "cromfs-inodefun.hh"

#include <sys/stat.h>
#include <cerrno>

uint_fast64_t GetInodeSize(const cromfs_inode_internal& inode, uint_fast32_t storage_opts)
{
    uint_fast64_t result = INODE_SIZE_BYTES(inode.blocklist.size());
    // Round up to be evenly divisible by 4.
    result = (result + 3) & ~3;
    return result;
}

uint_fast64_t GetInodeOffset(cromfs_inodenum_t inonum)
{
    /* Returns a byte offset into inotab when given an inode number. */
    return (inonum-2) * UINT64_C(4);
}

cromfs_inodenum_t get_first_free_inode_number(uint_fast64_t inotab_size)
{
    /* This is the formula for making inode number from inode address */
    return ((inotab_size+3)/4)+2;
}

const std::vector<unsigned char>
    encode_inode(const cromfs_inode_internal& inode,
                 uint_fast32_t storage_opts)
{
    std::vector<unsigned char> result(GetInodeSize(inode, storage_opts));
    put_inode(&result[0], inode, storage_opts);
    return result;
}

void put_inode(unsigned char* inodata,
               const cromfs_inode_internal& inode,
               uint_fast32_t storage_opts)
{
    uint_fast32_t rdev_links = inode.links;
    if(S_ISCHR(inode.mode) || S_ISBLK(inode.mode)) rdev_links = inode.rdev;

    W32(&inodata[0x00], inode.mode);
    W32(&inodata[0x04], inode.time);
    W32(&inodata[0x08], rdev_links);
    W16(&inodata[0x0C], inode.uid);
    W16(&inodata[0x0E], inode.gid);
    W64(&inodata[0x10], inode.bytesize);

    if(storage_opts & CROMFS_OPT_VARIABLE_BLOCKSIZES)
        W32(&inodata[0x18], inode.blocksize);

    /* Endianess safe. */

    const unsigned b = BLOCKNUM_SIZE_BYTES(), headersize = INODE_HEADER_SIZE();
    for(unsigned a=0; a<inode.blocklist.size(); ++a)
        Wn(&inodata[headersize+a*b], inode.blocklist[a], b);
}

void get_inode
   (const unsigned char* inodata, uint_fast64_t inodata_size,
    cromfs_inode_internal& inode,
    uint_fast32_t storage_opts,
    uint_fast32_t bsize,
    bool and_blocks)
{
    const uint_fast64_t headersize = INODE_HEADER_SIZE();
    if(inodata_size > 0 && inodata_size < headersize) throw EIO;

    uint_fast32_t rdev_links;
    inode.mode    = R32(inodata+0x0000);
    inode.time    = R32(inodata+0x0004);
    rdev_links    = R32(inodata+0x0008);
    inode.uid     = R16(inodata+0x000C);
    inode.gid     = R16(inodata+0x000E);
    inode.bytesize= R64(inodata+0x0010);

    if(storage_opts & CROMFS_OPT_VARIABLE_BLOCKSIZES)
        inode.blocksize = bsize = R32(inodata+0x0018);
    else
        inode.blocksize = bsize;

    if(S_ISCHR(inode.mode) || S_ISBLK(inode.mode))
        { inode.links = 1; inode.rdev = rdev_links; }
    else
        { inode.links = rdev_links; inode.rdev = 0; }

    // if(S_ISDIR(inode.mode)) inode.links += 2; /* For . and .. */

    if(and_blocks)
    {
        const uint_fast32_t block_bytesize = BLOCKNUM_SIZE_BYTES();

        uint_fast64_t nblocks = CalcSizeInBlocks(inode.bytesize, bsize);
        inode.blocklist.resize(nblocks);

        if(inodata_size > 0 && inodata_size < headersize+block_bytesize*nblocks)
        {
            /* Invalid inode */
            throw EIO;
        }

        uint_fast64_t n = nblocks;
        while(n-- > 0)
            inode.blocklist[n] = Rn(&inodata[headersize + block_bytesize*n], block_bytesize);
    }
}

void increment_inode_linkcount(unsigned char* inodata)
{
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

uint_fast64_t CalcSizeInBlocks(uint_fast64_t filesize, uint_fast32_t bsize)
{
    return (filesize + bsize-1) / bsize;
}

void PutInodeSize(cromfs_inode_internal& inode, uint_fast64_t bytesize)
{
    inode.bytesize = bytesize;
    inode.blocklist.resize( CalcSizeInBlocks(bytesize, inode.blocksize) );
}
