#include "cromfs-blockindex.hh"
#include "util/mkcromfs_sets.hh"
#include "util.hh" // For ReportSize

#include "cromfs-hashmap_lzo_sparse.hh"
//#include "cromfs-hashmap_lzo.hh"
//#include "cromfs-hashmap_googlesparse.hh"
//#include "cromfs-hashmap_sparsefile.hh"

#include <cstring>
#include <sstream>

template<typename Key, typename Value>
class block_index_stack<Key,Value>::layer
    : public CompressedHashLayer_Sparse<Key,Value>
//    : public CompressedHashLayer<Key,Value>
//    : public CacheFile<Key,Value>
//    : public GoogleSparseMap<Key,Value>
{
};

template<typename Key, typename Value>
bool block_index_stack<Key,Value>::Find(Key crc, Value& result,     size_t find_index) const
{
    if(find_index >= index.size()) return false;
    if(!index[find_index]->has(crc)) return false;
    index[find_index]->extract(crc, result);
    return true;
}

template<typename Key, typename Value>
void block_index_stack<Key,Value>::Add(Key crc, const Value& value)
{
    for(size_t ind = 0; ind < index.size(); ++ind)
    {
        layer& lay = *index[ind];
        if(!lay.has(crc))
        {
            lay.set(crc, value);
            ++size;
            return;
        }

        // Removed: Assume the caller knows not to add
        //          something that already exists.
        /*
        Value tmp;
        lay.extract(crc, tmp);
        if(tmp == value) return;*/
    }
    size_t ind = index.size();
    index.push_back(new layer);
    index[ind]->set(crc, value);
    ++size;
}

template<typename Key, typename Value>
void block_index_stack<Key,Value>::Del(Key crc, const Value& value)
{
    for(size_t ind = 0; ind < index.size(); ++ind)
    {
        layer& lay = *index[ind];
        if(!lay.has(crc))
        {
            break;
        }
        Value tmp;
        lay.extract(crc, tmp);
        if(tmp == value)
        {
            lay.unset(crc);
            --size;
            ++deleted;
            return;
        }
    }
}

/*
bool block_index_type::EmergencyFreeSpace(bool Auto, bool Real)
{
    return false;
}
*/

/*template<typename Key, typename Value>
void block_index_stack<Key,Value>::Clone()
{
    for(size_t a=0; a<index.size(); ++a)
        index[a] = new layer(*index[a]);
}*/

template<typename Key, typename Value>
void block_index_stack<Key,Value>::Close()
{
    for(size_t a=0; a<index.size(); ++a) delete index[a];
    index.clear();
}

template<typename Key, typename Value>
block_index_stack<Key,Value>::block_index_stack()
    : index(), size(0), deleted(0)
{
}

/*
template<typename Key, typename Value>
block_index_stack<Key,Value>::block_index_stack(const block_index_stack<Key,Value>& b)
    : index(b.index),
      size(b.size),
      deleted(b.deleted)
{
    Clone();
}

template<typename Key, typename Value>
block_index_stack<Key,Value>&
block_index_stack<Key,Value>::operator= (const block_index_stack<Key,Value>& b)
{
    if(&b != this)
    {
        Close();
        index = b.index;
        size  = b.size;
        deleted = b.deleted;
        Clone();
    }
    return *this;
}
*/

template<typename Key, typename Value>
void block_index_stack<Key,Value>::clear()
{
    Close();
    index.clear();
    size = deleted = 0;
}

/*
std::string block_index_type::get_usage() const
{
    std::stringstream tmp;

    tmp << " index_use:r=" << real.size
        << ";a=" << autom.size
        << ",-" << autom.deleted;
    return tmp.str();
}
*/

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

/*
block_index_type* block_index_global = 0;
*/


typedef block_index_stack<unsigned,uint_least32_t> si;
template si::block_index_stack();
//template si& si::operator=(const si&);
//template si::block_index_stack(const si&);
template void si::clear();
template bool si::Find(unsigned crc, uint_least32_t& result, size_t find_index) const;
template void si::Add(unsigned crc, const uint_least32_t& value);
template void si::Del(unsigned crc, const uint_least32_t& value);
template void si::Close();
//template void si::Clone();

typedef block_index_stack<BlockIndexHashType,cromfs_block_internal> ai;
template ai::block_index_stack();
//template ai& ai::operator=(const ai&);
//template ai::block_index_stack(const ai&);
template void ai::clear();
template bool ai::Find(BlockIndexHashType crc, cromfs_block_internal& result, size_t find_index) const;
template void ai::Add(BlockIndexHashType crc, const cromfs_block_internal& value);
template void ai::Del(BlockIndexHashType crc, const cromfs_block_internal& value);
template void ai::Close();
//template void ai::Clone();
