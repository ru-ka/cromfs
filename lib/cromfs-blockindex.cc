#include "cromfs-blockindex.hh"
#include "util/mkcromfs_sets.hh"
#include "util.hh" // For ReportSize

#include "cromfs-hashmap_lzo.hh"
#include "cromfs-hashmap_googlesparse.hh"

#include <cstring>
#include <sstream>

class block_index_type::realindex_layer
    : public GoogleSparseMap    <BlockIndexHashType, cromfs_blocknum_t>
{
};

class block_index_type::autoindex_layer
#ifdef OPTIMAL_GOOGLE_SPARSETABLE
    : public GoogleSparseMap    <BlockIndexHashType, cromfs_block_internal>
#else
    : public CompressedHashLayer<BlockIndexHashType, cromfs_block_internal>
#endif
{
};

bool block_index_type::FindRealIndex(BlockIndexHashType crc, cromfs_blocknum_t& result,     size_t find_index) const
{
    if(find_index >= realindex.size()) return false;
    if(!realindex[find_index]->has(crc)) return false;
    realindex[find_index]->extract(crc, result);
    return true;
}

bool block_index_type::FindAutoIndex(BlockIndexHashType crc, cromfs_block_internal& result, size_t find_index) const
{
    if(find_index >= autoindex.size()) return false;
    if(!autoindex[find_index]->has(crc)) return false;
    autoindex[find_index]->extract(crc, result);
    return true;
}

void block_index_type::AddRealIndex(BlockIndexHashType crc, cromfs_blocknum_t value)
{
    for(size_t index = 0; index < realindex.size(); ++index)
    {
        realindex_layer& layer = *realindex[index];
        if(!layer.has(crc))
        {
            layer.set(crc, value);
            ++n_real;
            return;
        }
        cromfs_blocknum_t tmp;
        layer.extract(crc, tmp);
        if(tmp == value) return;
    }
    size_t index = realindex.size();
    realindex.push_back(new realindex_layer);
    realindex[index]->set(crc, value);
    ++n_real;
}

void block_index_type::AddAutoIndex(BlockIndexHashType crc, const cromfs_block_internal& value)
{
    for(size_t index = 0; index < autoindex.size(); ++index)
    {
        autoindex_layer& layer = *autoindex[index];
        if(!layer.has(crc))
        {
            layer.set(crc, value);
            ++n_auto;
            return;
        }
        cromfs_block_internal tmp;
        layer.extract(crc, tmp);
        if(tmp == value) return;
    }
    size_t index = autoindex.size();
    autoindex.push_back(new autoindex_layer);
    autoindex[index]->set(crc, value);
    ++n_auto;
}

void block_index_type::DelAutoIndex(BlockIndexHashType crc, const cromfs_block_internal& value)
{
    for(size_t index = 0; index < autoindex.size(); ++index)
    {
        autoindex_layer& layer = *autoindex[index];
        if(!layer.has(crc))
        {
            break;
        }
        cromfs_block_internal tmp;
        layer.extract(crc, tmp);
        if(tmp == value)
        {
            layer.unset(crc);
            --n_auto;
            ++n_auto_deleted;
            return;
        }
    }
}

bool block_index_type::EmergencyFreeSpace(bool Auto, bool Real)
{
    return false;
}

void block_index_type::Clone()
{
    for(size_t a=0; a<realindex.size(); ++a)
        realindex[a] = new realindex_layer(*realindex[a]);

    for(size_t a=0; a<autoindex.size(); ++a)
        autoindex[a] = new autoindex_layer(*autoindex[a]);
}

void block_index_type::Close()
{
    for(size_t a=0; a<realindex.size(); ++a) delete realindex[a];
    for(size_t a=0; a<autoindex.size(); ++a) delete autoindex[a];
    realindex.clear();
    autoindex.clear();
}

block_index_type::block_index_type()
    : realindex(), autoindex(),
      n_real(0), n_auto(0), n_auto_deleted(0)
{
}

block_index_type::block_index_type(const block_index_type& b)
    : realindex(b.realindex),
      autoindex(b.autoindex),
      n_real(b.n_real),
      n_auto(b.n_auto),
      n_auto_deleted(b.n_auto_deleted)
{
    Clone();
}

block_index_type& block_index_type::operator= (const block_index_type& b)
{
    if(&b != this)
    {
        Close();
        realindex = b.realindex;
        autoindex = b.autoindex;
        n_real    = b.n_real;
        n_auto    = b.n_auto;
        n_auto_deleted = b.n_auto_deleted;
        Clone();
    }
    return *this;
}

void block_index_type::clear()
{
    Close();
    realindex.clear();
    autoindex.clear();
    n_real = 0;
    n_auto = 0;
    n_auto_deleted = 0;
}

std::string block_index_type::get_usage() const
{
    std::stringstream tmp;

    tmp << " index_use:r=" << n_real
        << ";a=" << n_auto
        << ",-" << n_auto_deleted;
    return tmp.str();
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


block_index_type* block_index_global = 0;
