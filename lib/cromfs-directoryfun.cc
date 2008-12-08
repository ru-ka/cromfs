#include <cstring>
#include "endian.hh"
#include "cromfs-directoryfun.hh"

#include <cstring> // std::memcpy

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
    
    W32(&result[0], dir.size());
    
    unsigned entrytableoffs = result.size();
    unsigned entryoffs = 0;
    
    unsigned diroffset=0;
    for(cromfs_dirinfo::const_iterator i = dir.begin(); i != dir.end(); ++i)
    {
        const std::string&     name = i->first;
        const cromfs_inodenum_t ino = i->second;
        
        W32(&result[4 + diroffset*4], entrytableoffs + entryoffs);
        
        entries.resize(entryoffs + 8 + name.size() + 1);
        
        W64(&entries[entryoffs], ino);
        std::memcpy(&entries[entryoffs+8], name.c_str(), name.size()+1);
        //^ basically strcpy, but since we already know the length, this is faster
        
        entryoffs = entries.size();
        ++diroffset;
    }
    // append the name buffer to the pointer buffer.
    result.insert(result.end(), entries.begin(), entries.end());
    return result;
}
