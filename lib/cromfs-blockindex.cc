#include "cromfs-blockindex.hh"
#include "util/mkcromfs_sets.hh"
#include "util.hh" // For ReportSize

#include <cstring>

#include "minilzo.h"

void block_index_type::Init()
{
    realindex.reserve(32);
    autoindex.reserve(32);
}



bool block_index_type::FindRealIndex(BlockIndexHashType crc, cromfs_blocknum_t& result,     size_t find_index) const
{
    if(find_index >= realindex.size()) return false;
    if(realindex[find_index].hashbits.find(crc)
    == realindex[find_index].hashbits.end()) return false;
    realindex[find_index].extract(crc, result);
    return true;
}

bool block_index_type::FindAutoIndex(BlockIndexHashType crc, cromfs_block_internal& result, size_t find_index) const
{
    if(find_index >= autoindex.size()) return false;
    if(autoindex[find_index].hashbits.find(crc)
    == autoindex[find_index].hashbits.end()) return false;
    autoindex[find_index].extract(crc, result);
    return true;
}

void block_index_type::AddRealIndex(BlockIndexHashType crc, cromfs_blocknum_t value)
{
    for(size_t index = 0; index < realindex.size(); ++index)
    {
        CompressedHashLayer<cromfs_blocknum_t>& layer = realindex[index];
        if(layer.hashbits.find(crc) == layer.hashbits.end())
        {
            layer.set(crc, value);
            return;
        }
        cromfs_blocknum_t tmp;
        layer.extract(crc, tmp);
        if(tmp == value) return;
    }
    size_t index = realindex.size();
    realindex.resize(index+1);
    realindex[index].set(crc, value);
}

void block_index_type::AddAutoIndex(BlockIndexHashType crc, const cromfs_block_internal& value)
{
    for(size_t index = 0; index < autoindex.size(); ++index)
    {
        CompressedHashLayer<cromfs_block_internal>& layer = autoindex[index];
        if(layer.hashbits.find(crc) == layer.hashbits.end())
        {
            layer.set(crc, value);
            return;
        }
        cromfs_block_internal tmp;
        layer.extract(crc, tmp);
        if(tmp == value) return;
    }
    size_t index = autoindex.size();
    autoindex.resize(index+1);
    autoindex[index].set(crc, value);
}

void block_index_type::DelAutoIndex(BlockIndexHashType crc, const cromfs_block_internal& value)
{
    for(size_t index = 0; index < autoindex.size(); ++index)
    {
        CompressedHashLayer<cromfs_block_internal>& layer = autoindex[index];
        if(layer.hashbits.find(crc) == layer.hashbits.end())
        {
            break;
        }
        cromfs_block_internal tmp;
        layer.extract(crc, tmp);
        if(tmp == value)
        {
            layer.unset(crc);
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

template<typename T>
void block_index_type::
    CompressedHashLayer<T>::extract(BlockIndexHashType crc, T& result) const
{
    const
    size_t bucket = crc / n_per_bucket,
           bucketpos = (crc % n_per_bucket) * sizeof(T);

    (const_cast<CompressedHashLayer<T>*> (this))->
    load(bucket);

    std::memcpy(&result, &dirtybucket[bucketpos], sizeof(T));
}

template<typename T>
void block_index_type::
    CompressedHashLayer<T>::set(BlockIndexHashType crc, const T& result)
{
    const
    size_t bucket = crc / n_per_bucket,
           bucketpos = (crc % n_per_bucket) * sizeof(T);

    load(bucket);

    if(dirtybucket.size() < bucketpos + sizeof(T))
        dirtybucket.resize(bucketpos + sizeof(T));

    std::memcpy(&dirtybucket[bucketpos], &result, sizeof(T));
    hashbits.set(crc, crc+1);
    dirtystate = rw;
}

template<typename T>
void block_index_type::
    CompressedHashLayer<T>::unset(BlockIndexHashType crc)
{
    hashbits.erase(crc);
}

template<typename T>
block_index_type::
    CompressedHashLayer<T>::CompressedHashLayer()
    : hashbits(),
      dirtybucket(),
      dirtybucketno(n_buckets),
      dirtystate(none)
{
}

template<typename T>
void block_index_type::
    CompressedHashLayer<T>::flushdirty()
{
    if(dirtystate == rw)
    {
        size_t actual_bucketsize = dirtybucket.size();
#if 1
        const size_t decom_max = bucketsize+bucketsize/16+64+3;
        static unsigned char decombuf[decom_max];
        lzo_uint destlen = decom_max;
        char wrkmem[LZO1X_1_MEM_COMPRESS];
        lzo1x_1_compress(&dirtybucket[0], actual_bucketsize,
                         decombuf, &destlen,
                         wrkmem);
        buckets[dirtybucketno].assign(decombuf, decombuf+destlen);
#else
        buckets[dirtybucketno] = dirtybucket;
#endif
        dirtystate = ro;
    }
}

template<typename T>
void block_index_type::
    CompressedHashLayer<T>::load(size_t bucketno)
{
    if(dirtystate == none || dirtybucketno != bucketno)
    {
        flushdirty();

        lzo_uint destlen = 0;
        if(!buckets[bucketno].empty())
        {
#if 1
            destlen = bucketsize;
            dirtybucket.resize(bucketsize);
            lzo1x_decompress(&buckets[bucketno][0], buckets[bucketno].size(),
                             &dirtybucket[0], &destlen,
                             0);
#else
            destlen = buckets[bucketno].size();
            dirtybucket = buckets[bucketno];
#endif
        }

        dirtybucket.resize(destlen);
        dirtybucketno = bucketno;
        dirtystate = ro;
    }
}
