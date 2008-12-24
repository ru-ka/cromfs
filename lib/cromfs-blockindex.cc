#include "cromfs-blockindex.hh"
#include "util/mkcromfs_sets.hh"
#include "util.hh" // For ReportSize

void block_index_type::Init()
{
    HashIndexType dummyhash = UINT64_C(0xFFFFFFFF) << 32;
    //realindex.set_deleted_key(dummyhash);
    autoindex.set_deleted_key(dummyhash);
}

bool block_index_type::FindRealIndex(BlockIndexHashType crc, cromfs_blocknum_t& result,     size_t find_index) const
{
    HashIndexType hash = crc | (HashIndexType(find_index) << 32);
    realindex_type::const_iterator i(realindex.find(hash));
    if(i == realindex.end()) return false;
    result = i->second;
    return true;
}

bool block_index_type::FindAutoIndex(BlockIndexHashType crc, cromfs_block_internal& result, size_t find_index) const
{
    HashIndexType hash = crc | (HashIndexType(find_index) << 32);
    autoindex_type::const_iterator i(autoindex.find(hash));
    if(i == autoindex.end()) return false;
    result = i->second;
    return true;
}

void block_index_type::AddRealIndex(BlockIndexHashType crc, cromfs_blocknum_t value)
{
    for(size_t index = 0; ; ++index)
    {
        HashIndexType hash = crc | (HashIndexType(index) << 32);
        realindex_type::iterator i(realindex.find(hash));
        if(i != realindex.end())
        {
            if(i->second == value) return;
            continue;
        }
        realindex.insert(i, std::make_pair(hash, value));
        return;
    }
}

void block_index_type::AddAutoIndex(BlockIndexHashType crc, const cromfs_block_internal& value)
{
    for(size_t index = 0; ; ++index)
    {
        HashIndexType hash = crc | (HashIndexType(index) << 32);
        autoindex_type::iterator i(autoindex.find(hash));
        if(i != autoindex.end())
        {
            if(i->second == value) return;
            continue;
        }
        autoindex.insert(i, std::make_pair(hash, value));
        return;
    }
}

void block_index_type::DelAutoIndex(BlockIndexHashType crc, const cromfs_block_internal& value)
{
    for(size_t index = 0; ; ++index)
    {
        HashIndexType hash = crc | (HashIndexType(index) << 32);
        autoindex_type::iterator i(autoindex.find(hash));
        if(i == autoindex.end()) break;
        if(i->second == value)
        {
            autoindex.erase(i);
            break;
        }
    }
}

bool block_index_type::EmergencyFreeSpace(bool Auto, bool Real)
{
    return false;
}

void block_index_type::Clone()
{
}

void block_index_type::Close()
{
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


block_index_type* block_index_global;
