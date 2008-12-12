/*
cromfs - Copyright (C) 1992,2008 Bisqwit (http://iki.fi/bisqwit/)
Licence: GPL

cromfs-write.hh: Definitions of structures needed for write access
on a cromfs volume.

*/

#include "cromfs-defs.hh"
#include "rangeset.hh"
#include "rangemultimap.hh"

class cromfs_write_data
{
    /* TODO: Also needs a map of users of each BLKDATA entry */
    /* TODO: The same range of the fblock can be used by different
     *       portions of the same inode. Hence this fblock_utilizers_list
     *       is inadequate. There should also be a refcount. Or the key
     *       should be the block number.
     */

    /* List of areas of the fblock which are utilized at all.
     * Invert to get list of free areas. */
    typedef rangeset<uint_fast64_t/*offset*/>
        fblock_utilization_map;

    /* List of objects that are utilizing the particular fblock
     */
    typedef rangemultimap<uint_fast64_t, cromfs_inodenum_t>
        fblock_utilizers_list;

    struct fblock_info
    {
        fblock_utilizers_list utlist;
        fblock_utilization_map utmap;

        void rebuild();

        void allocate_range_for(
            uint_fast64_t start, uint_fast64_t size,
            cromfs_inodenum_t inonum);

        void deallocate_range_of(
            uint_fast64_t start, uint_fast64_t size,
            cromfs_inodenum_t inonum);
    };

private:
    std::vector<fblock_info> fblockinfo;
};
