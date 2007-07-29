#include "cromfs-inodefun.hh"

#include <sys/stat.h>
#include <cerrno>

unsigned GetInodeSize(const cromfs_inode_internal& inode, unsigned blocknum_size_bytes)
{
    unsigned result = INODE_BLOCKLIST_OFFSET;
    
    result += inode.blocklist.size() * blocknum_size_bytes;
    
    // Round up to be evenly divisible by 4.
    result = (result + 3) & ~3;
    
    return result;
}

const uint_fast64_t GetInodeOffset(cromfs_inodenum_t inonum)
{
    /* Returns a byte offset into inotab when given an inode number. */
    return (inonum-2) * UINT64_C(4);
}

const std::vector<unsigned char>
    encode_inode(const cromfs_inode_internal& inode,
                 unsigned blocknum_size_bytes)
{
    std::vector<unsigned char> result(GetInodeSize(inode, blocknum_size_bytes));
    put_inode(&result[0], inode, blocknum_size_bytes);
    return result;
}

void put_inode(unsigned char* inodata,
               const cromfs_inode_internal& inode,
               unsigned blocknum_size_bytes)
{
    uint_fast32_t rdev_links = inode.links;
    if(S_ISCHR(inode.mode) || S_ISBLK(inode.mode)) rdev_links = inode.rdev;

    W32(&inodata[0x00], inode.mode);
    W32(&inodata[0x04], inode.time);
    W32(&inodata[0x08], rdev_links);
    W16(&inodata[0x0C], inode.uid);
    W16(&inodata[0x0E], inode.gid);
    W64(&inodata[0x10], inode.bytesize);
    
    /* Endianess safe. */
    
    const unsigned b = blocknum_size_bytes;
    for(unsigned a=0; a<inode.blocklist.size(); ++a)
        Wn(&inodata[INODE_BLOCKLIST_OFFSET+a*b], inode.blocklist[a], b);
}

void get_inode(const unsigned char* inodata, 
               cromfs_inode_internal& inode,
               uint_fast32_t bsize,
               uint_fast32_t blocknum_size_bytes)
{
    get_inode(inodata,0,inode,bsize,blocknum_size_bytes);
}

void get_inode(const unsigned char* inodata, uint_fast64_t inodata_size,
               cromfs_inode_internal& inode,
               uint_fast32_t bsize,
               uint_fast32_t block_bytesize)
{
    if(inodata_size > 0 && inodata_size < INODE_BLOCKLIST_OFFSET) throw EIO;
    
    uint_fast32_t rdev_links;
    inode.mode    = R32(inodata+0x0000);  
    inode.time    = R32(inodata+0x0004);
    rdev_links    = R32(inodata+0x0008);
    inode.uid     = R16(inodata+0x000C);
    inode.gid     = R16(inodata+0x000E);
    inode.bytesize= R64(inodata+0x0010);
    
    if(S_ISCHR(inode.mode) || S_ISBLK(inode.mode))
        { inode.links = 1; inode.rdev = rdev_links; }
    else
        { inode.links = rdev_links; inode.rdev = 0; }
    
    // if(S_ISDIR(inode.mode)) inode.links += 2; /* For . and .. */
    
    if(bsize && block_bytesize)
    {
        uint_fast64_t nblocks = CalcSizeInBlocks(inode.bytesize, bsize);
        inode.blocklist.resize(nblocks);
        
        if(inodata_size > 0 && inodata_size < block_bytesize*nblocks+INODE_BLOCKLIST_OFFSET)
        {
            /* Invalid inode */
            throw EIO;
        }
        
        uint_fast64_t n = nblocks;
        while(n-- > 0)
            inode.blocklist[n] = Rn(&inodata[INODE_BLOCKLIST_OFFSET + block_bytesize*n], block_bytesize);
    }
}

const cromfs_inodenum_t get_first_free_inode_number(uint_fast64_t inotab_size)
{
    /* This is the formula for making inode number from inode address */
    return ((inotab_size+3)/4)+2;
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

void PutInodeSize(cromfs_inode_internal& inode,
                  uint_fast64_t bytesize,
                  long bsize)
{
    inode.bytesize = bytesize;
    inode.blocklist.resize( CalcSizeInBlocks(bytesize, bsize) );
}
