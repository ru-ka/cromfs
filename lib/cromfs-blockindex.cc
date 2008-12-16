#include "lib/endian.hh"

#include <fcntl.h>

#include <sstream>
#include "cromfs-blockindex.hh"
#include "util/mkcromfs_sets.hh"


/*
Note: These functions perform a syscall for EVERY read and write,
which is somewhat anxious for performance. However, considering
that this is a HASH structure, it is expected that there will be
little to no chances for caching. So no mmap() optimizations here.
*/

bool block_index_type::FindRealIndex(BlockIndexHashType crc, cromfs_blocknum_t& result,     size_t find_index) const
{
    if(find_index >= realindex_fds.size()) return false;
    char Packet[4] = {0};
    if(pread64( realindex_fds[find_index], Packet, 4, RealPos(crc)) <= 0) return false;
    result = R32(Packet);
    if(result == 0)
        return false; // hole in a file apparently

    //printf("FindRealIndex(%08X, %u)->block %u\n", crc, (unsigned)find_index, (unsigned)result);

    return true;
}

bool block_index_type::FindAutoIndex(BlockIndexHashType crc, cromfs_block_internal& result, size_t find_index) const
{
    if(find_index >= autoindex_fds.size()) return false;
    char Packet[4+4] = {0};
    if(pread64( autoindex_fds[find_index], Packet, 4+4, AutoPos(crc)) <= 0) return false;
    result.fblocknum = R32(Packet);
    result.startoffs = R32(Packet+4);
    if(result.fblocknum == 0 && result.startoffs == 0)
        return false; // hole in a file apparently

    //printf("FindAutoIndex(%08X, %u)->fblock %u:%u\n", crc, (unsigned)find_index, (unsigned)result.fblocknum, result.startoffs);

    return true;
}

void block_index_type::AddRealIndex(BlockIndexHashType crc, cromfs_blocknum_t value)
{
    size_t find_index = 0; cromfs_blocknum_t tmp;
    while(find_index < realindex_fds.size())
    {
        char Packet[4] = {0};
        if(pread64( realindex_fds[find_index], Packet, 4, RealPos(crc)) <= 0) break;
        tmp = R32(Packet);
        if(tmp == 0) break;
        if(tmp == value) return;

        ++find_index;
    }
    if(find_index >= realindex_fds.size())
    {
        printf("hash %08X demands a new RealIndex file (number %u)\n", crc, (unsigned)find_index);
        find_index = new_real();
    }

    char Packet[4];
    W32(Packet, value);
    pwrite64( realindex_fds[find_index], Packet, 4, RealPos(crc));
}

void block_index_type::AddAutoIndex(BlockIndexHashType crc, const cromfs_block_internal& value)
{
    size_t find_index = 0; cromfs_block_internal tmp;
    while(find_index < autoindex_fds.size())
    {
        char Packet[4+4] = {0};
        if(pread64( autoindex_fds[find_index], Packet, 4+4, AutoPos(crc)) <= 0) break;
        tmp.fblocknum = R32(Packet);
        tmp.startoffs = R32(Packet+4);
        if(tmp.fblocknum == 0 && tmp.startoffs == 0) break;
        if(tmp == value) return;
        ++find_index;
    }
    if(find_index >= autoindex_fds.size())
    {
        printf("hash %08X demands a new AutoIndex file (number %u)\n", crc, (unsigned)find_index);
        find_index = new_auto();
    }

    char Packet[4+4];
    W32(Packet,   value.fblocknum);
    W32(Packet+4, value.startoffs);
    pwrite64( autoindex_fds[find_index], Packet, 4+4, AutoPos(crc));
}

void block_index_type::DelAutoIndex(BlockIndexHashType crc, const cromfs_block_internal& value)
{
    cromfs_block_internal tmp;
    for(size_t find_index=0; find_index < autoindex_fds.size(); ++find_index)
    {
        if(!FindAutoIndex(crc, tmp, find_index)) break;
        if(tmp == value)
        {
            /* Ideally we would punch a hole into the file, but this is
             * the next best option
             */
            char Packet[4+4] = { 0 };
            pwrite64( autoindex_fds[find_index], Packet, 4+4, AutoPos(crc));
            return;
        }
    }
}

const std::string block_index_type::GetRealFn(size_t index) const
{
    std::stringstream tmp;
    tmp << GetTempDir() << "/real-" << (void*)(this) << "-" << index << ".dat";
    return tmp.str();
}

const std::string block_index_type::GetAutoFn(size_t index) const
{
    std::stringstream tmp;
    tmp << GetTempDir() << "/auto-" << (void*)(this) << "-" << index << ".dat";
    return tmp.str();
}

/* Note: Not threadsafe */
size_t block_index_type::new_real()
{
    const size_t result = realindex_fds.size();
    std::string fn = GetRealFn(result);

    std::printf("Opening new RealIndex file: %s\n", fn.c_str());

    int fd = open(fn.c_str(), O_RDWR | O_TRUNC | O_CREAT | O_NOATIME | O_EXCL, 0600);
    if(fd < 0) perror(("new_real:"+fn).c_str()); // TODO: A better error resolution

    unlink(fn.c_str());
    realindex_fds.push_back(fd);
    return result;
}


/* Note: Not threadsafe */
size_t block_index_type::new_auto()
{
    const size_t result = autoindex_fds.size();
    std::string fn = GetAutoFn(result);

    std::printf("Opening new AutoIndex file: %s\n", fn.c_str());

    int fd = open(fn.c_str(), O_RDWR | O_TRUNC | O_CREAT | O_NOATIME | O_EXCL, 0600);
    if(fd < 0) perror(("new_auto:"+fn).c_str()); // TODO: A better error resolution

    unlink(fn.c_str());
    autoindex_fds.push_back(fd);
    return result;
}


/*************************************************
  Goals of this hash calculator:

  1. Minimize chances of duplicate matches
  2. Increase data locality (similar data gives similar hashes)
  3. Be fast to calculate

  Obviously, aiming for goal #2 subverts goal #1.

  Note: Among goals is _not_:
  1. Endianess safety
  The hash will never be exposed outside this program,
  so it does not need to be endian safe.

**************************************************/

#include "newhash.h"
BlockIndexHashType
    BlockIndexHashCalc(const unsigned char* buf, unsigned long size)
{
    return newhash_calc(buf, size);
}