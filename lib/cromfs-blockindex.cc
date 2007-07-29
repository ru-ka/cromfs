#include "cromfs-blockindex.hh"

bool block_index_type::FindByCRC(
    crc32_t crc,
    mkcromfs_block_location& result,
    size_t find_index) const
{
    if(crc != last_search.crc
    || find_index < last_search.index)
    {
        last_search.crc   = crc;
        last_search.i     = index.find(crc);
        last_search.index = 0;
    }
    if(last_search.i == index.end()) return false;

    while(last_search.index < find_index)
    {
        ++last_search.i;
        ++last_search.index;

        if(last_search.i == index.end()) return false;
        
        if(last_search.i.get_key() != crc)
        //if(last_search.i->first != crc)
        {
            last_search.i = index.end();
            return false;
        }
    }
    if(last_search.i == index.end())
        return false;
    
    result = last_search.i.get_value();
    //result = last_search.i->second;
    return true;
}

void block_index_type::Add(
    crc32_t crc,
    cromfs_fblocknum_t fnum,
    uint_fast32_t startoffs,
    cromfs_blocknum_t bnum)
{
    last_search.reset();
    
    index.insert(std::make_pair(crc,
        mkcromfs_block_location(fnum, startoffs, bnum)));
}

void block_index_type::Update(
    crc32_t crc,
    cromfs_fblocknum_t fnum,
    uint_fast32_t startoffs,
    cromfs_blocknum_t bnum)
{
    mkcromfs_block_location restmp;
    for(size_t index=0; FindByCRC(crc, restmp, index); ++index)
    {
        if(restmp.fblocknum != fnum
        || restmp.startoffs != startoffs) continue;
        
        mkcromfs_block_location& block = last_search.i.get_value_mutable();
        if(block.fblocknum == fnum
        && block.startoffs == startoffs)
        {
            block.blocknum = bnum;
            return;
        }
    }
}

block_index_type::block_index_type(const block_index_type& b)
    : index(b.index), last_search()
{
}

block_index_type& block_index_type::operator=(const block_index_type& b)
{
    if(&b != this)
    {
        last_search.reset();
        index = b.index;
    }
    return *this;
}
