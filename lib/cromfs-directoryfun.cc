#include <cstring>
#include "endian.hh"
#include "cromfs-directoryfun.hh"

#include <cstring> // std::memcpy

/* Directory format:
****

FILE CONTENT WHEN: DIRECTORY (size: 4 + (4+entrysize) * n)
	0000	u32	number of files in directory
	0004	u32[]	index into each file entry (from directory entry beginning)
	0004	ENTRY[]	each file entry (variable length)
	(Note: The files in a directory are sorted in an asciibetical order.
	 This enables an implementation of lookup() using a binary search,
	 minimizing the amount of data accessed (and thus yielding fast access
	 in case of heavy fragmentation).
	)
	(Note 2: Currently the algorithm in read_dir() assumes that the directory
	 entry pointers are in numeric order. If that does not hold, it will fail.
	)

STRUCT: ENTRY (size: 9 + n)
	0000	u64	inode number
	0008	char[]	file name, nul-terminated

****/

size_t
    calc_encoded_directory_size(const cromfs_dirinfo& dir)
{
    /* This function could simply be replaced with:
     *   return encode_directory(dir).size()
     * But that would be a waste of resources.
     */
    size_t result = 4; // the number of entries takes 4 bytes.
    for(cromfs_dirinfo::const_iterator i = dir.begin(); i != dir.end(); ++i)
    {
        result += 4 + 8 + 1 + i->first.size();
        // 4 for the pointer,
        // 8 for inode number,
        // n+1 for the filename.
    }
    return result;
}

const std::vector<unsigned char>
    encode_directory(const cromfs_dirinfo& dir)
{
    std::vector<unsigned char> result(4 + 4*dir.size()); // buffer for pointers
    std::vector<unsigned char> entries;                  // buffer for names
    entries.reserve(dir.size() * (8 + 10)); // 10 = guestimate of average fn length

    put_32(&result[0], dir.size());

    unsigned entrytableoffs = result.size();
    unsigned entryoffs = 0;

    unsigned diroffset=0;
    for(cromfs_dirinfo::const_iterator i = dir.begin(); i != dir.end(); ++i)
    {
        const std::string&     name = i->first;
        const cromfs_inodenum_t ino = i->second;

        put_32(&result[4 + diroffset*4], entrytableoffs + entryoffs);

        entries.resize(entryoffs + 8 + name.size() + 1);

        put_64(&entries[entryoffs], ino);
        std::memcpy(&entries[entryoffs+8], name.c_str(), name.size()+1);
        //^ basically strcpy, but since we already know the length, this is faster

        entryoffs = entries.size();
        ++diroffset;
    }
    // append the name buffer to the pointer buffer.
    result.insert(result.end(), entries.begin(), entries.end());
    return result;
}
