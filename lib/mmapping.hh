#ifndef bqtMmappingHH
#define bqtMmappingHH

//#define _LARGEFILE64_SOURCE
#include "endian.hh"
#include <unistd.h>
#include <sys/mman.h>

namespace {
static void* notmapped = (void*)-1;
}

enum { MMAP_PAGESIZE = 4096 };

template<bool AutoUnmap = true>
class MemMappingType
{
private:
public:
    MemMappingType() : ptr(notmapped), size(0), align_factor(0) { }

    MemMappingType(int fd, uint_fast64_t pos, uint_fast64_t length)
        : ptr(notmapped),size(0),align_factor(0)
    {
        SetMap(fd, pos, length);
    }

    ~MemMappingType()
    {
        if(AutoUnmap) Unmap();
    }

    void SetMap(int fd, uint_fast64_t pos, uint_fast64_t length)
    {
        Unmap();

        uint_fast64_t pos_aligned_down = pos & ~UINT64_C(MMAP_PAGESIZE-1);

        align_factor = pos - pos_aligned_down;

        size = length + align_factor;
        ptr =  mmap64(NULL, size,
                      PROT_READ, MAP_SHARED,
                      fd, pos_aligned_down);
    }

    void ReMapIfNecessary(int fd, uint_fast64_t pos, uint_fast64_t length)
    {
        uint_fast64_t pos_aligned_down = pos & ~UINT64_C(MMAP_PAGESIZE-1);
        size_t new_align_factor = pos - pos_aligned_down;
        size_t new_size         = length + align_factor;

        if(new_align_factor != align_factor
        || new_size         != size)
        {
            ptr = mremap(ptr, size, new_size, MREMAP_MAYMOVE);
            //fprintf(stderr, "did remap %lu->%lu\n", size, new_size);

            align_factor = new_align_factor;
            size         = new_size;
        }
        /*else
            fprintf(stderr, "%lu=%lu, not remapping\n",
                size,new_size);*/
    }

    void Unmap()
    {
        if(ptr != notmapped)
        {
            munmap(ptr, size);
            ptr = notmapped;
        }
    }

    operator bool() const
        { return ptr != notmapped; }

    const unsigned char* get_ptr() const
        { return ((const unsigned char*)ptr) + align_factor; }
    /*
    operator const unsigned char*() const
        { return get_ptr(); }
    const unsigned char* operator+ (uint_fast64_t f) const
        { return get_ptr() + f; }
    */

    MemMappingType(const MemMappingType<AutoUnmap>& b)
        : ptr(b.ptr), size(b.size), align_factor(b.align_factor)
    {
    }
    MemMappingType& operator=(const MemMappingType<AutoUnmap>& b)
    {
        ptr = b.ptr;
        size = b.size;
        align_factor = b.align_factor;
        return *this;
    }

private:
    void* ptr;
    size_t size;
    size_t align_factor;
};

typedef MemMappingType<true> MemMapping;

#endif
