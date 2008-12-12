#include "lib/endian.hh"

#include <fcntl.h>

#include <sstream>
#include "cromfs-blockindex.hh"
#include "util/mkcromfs_sets.hh"


/*
Note: These functions perform a syscall for EVERY read and write,
which is somewhat anxious for performance. However, considering
that this is a HASH structure, it is expected that there will
be little to no chances for caching.
*/

bool block_index_type::FindRealIndex(BlockIndexHashType crc, cromfs_blocknum_t& result,     size_t find_index) const
{
    if(find_index >= realindex_fds.size()) return false;
    char Packet[4] = {0};
    if(pread64( realindex_fds[find_index], Packet, 4, RealPos(crc)) <= 0) return false;
    result = R32(Packet);
    if(result == 0)
        return false; // hole in a file apparently
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
    return true;
}

void block_index_type::AddRealIndex(BlockIndexHashType crc, cromfs_blocknum_t value)
{
    size_t find_index = 0; cromfs_blocknum_t tmp;
    while(find_index < realindex_fds.size())
    {
        if(!FindRealIndex(crc, tmp, find_index)) break;
        if(tmp == value) return;
        ++find_index;
    }
    if(find_index >= realindex_fds.size()) find_index = new_real();

    char Packet[4];
    W32(Packet, value);
    pwrite64( realindex_fds[find_index], Packet, 4, RealPos(crc));
}

void block_index_type::AddAutoIndex(BlockIndexHashType crc, const cromfs_block_internal& value)
{
    size_t find_index = 0; cromfs_block_internal tmp;
    while(find_index < autoindex_fds.size())
    {
        if(!FindAutoIndex(crc, tmp, find_index)) break;
        if(tmp == value) return;
        ++find_index;
    }
    if(find_index >= autoindex_fds.size()) find_index = new_auto();

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

/* Note: Not threadsafe */
size_t block_index_type::new_real()
{
    const size_t result = realindex_fds.size();
    std::stringstream tmp;
    tmp << GetTempDir() << "/real-" << (void*)(this) << "-" << result << ".dat";
    const std::string fn = tmp.str();

    int fd = open(fn.c_str(), O_RDWR | O_TRUNC | O_CREAT | O_NOATIME | O_EXCL, 0600);
    if(fd < 0) perror(fn.c_str()); // TODO: A better error resolution

    unlink(fn.c_str());
    realindex_fds.push_back(fd);
    return result;
}


/* Note: Not threadsafe */
size_t block_index_type::new_auto()
{
    const size_t result = autoindex_fds.size();
    std::stringstream tmp;
    tmp << GetTempDir() << "/auto-" << (void*)(this) << "-" << result << ".dat";
    const std::string fn = tmp.str();

    int fd = open(fn.c_str(), O_RDWR | O_TRUNC | O_CREAT | O_NOATIME | O_EXCL, 0600);
    if(fd < 0) perror(fn.c_str()); // TODO: A better error resolution

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