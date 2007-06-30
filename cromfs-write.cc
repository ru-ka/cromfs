#include "cromfs-write.hh"

void cromfs_write_data::fblock_info::rebuild()
{
    utlist.clear();
    
    std::list<cromfs_inodenum_t> valuelist = utmap.get_valuelist();
    for(std::list<cromfs_inodenum_t>::const_iterator
        i = valuelist.begin(); i != valuelist.end(); ++i)
    {
        const rangeset<uint_fast64_t>& ranges = utmap.get_rangelist(*i);
        for(rangeset<uint_fast64_t>::const_iterator
            j = ranges.begin(); j != ranges.end(); ++j)
        {
            utlist.set(j->lower, j->upper);
        }
    }
}

void cromfs_write_data::fblock_info::allocate_range_for
    (uint_fast64_t start, uint_fast64_t size,
     cromfs_inodenum_t inonum);
{
    utmap.set(start, start+size, inonum);
}

void cromfs_write_data::fblock_info::deallocate_range_for
    (uint_fast64_t start, uint_fast64_t size,
     cromfs_inodenum_t inonum);
{
    utmap.erase(start, start+size, inonum);
}
