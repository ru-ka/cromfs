#ifndef bqtCromMmapVectorHH
#define bqtCromMmapVectorHH

#include "mmapping.hh"
#include "longfilewrite.hh"
#include "threadfun.hh"

#include <vector>
#include <utility>
#include <algorithm>

class mmap_vector_base;

class mmap_storage
{
public:
    mmap_storage(int f, uint_fast64_t c)
        : fd(f),
          alloc_cap(c),
          zero_cap(c)
    {
    }

private:
    friend class mmap_vector_base;

    int GetFd() const { return fd; }

    bool TryAllocExtend(uint_fast64_t offset, uint_fast64_t oldsize, uint_fast64_t newsize)
    {
        if(offset + oldsize == alloc_cap)
        {
            EnsureZeroUntil(alloc_cap = offset + newsize);
            return true;
        }
        return false;
    }

    uint_fast64_t Alloc(uint_fast64_t size)
    {
        uint_fast64_t begin =
            (alloc_cap + uint_fast64_t(MMAP_PAGESIZE-1)) & ~(MMAP_PAGESIZE-UINT64_C(1));

        alloc_cap = begin + size;

        EnsureZeroUntil(alloc_cap);
        return begin;
    }

private:
    void EnsureZeroUntil(uint_fast64_t cap)
    {
        /* Zero-fill the backing file until the given cap.
         * This avoids the ftruncate-mmap syndrome where
         * writing into mmapped hole of a sparse file causes
         * a segmentation failure or data loss.
         * According to the documentation on e.g.
         * http://www.gnu.org/software/libtool/manual/libc/File-Size.html ,
         * it _should_ work, but practice has shown that on
         * many filesystems, such as Reiser3, it does.not.work.
         */
        enum { blocksize = 16384 };
        static const unsigned char ZeroBlock[blocksize] = {0};

        while(zero_cap < cap)
        {
            size_t write = blocksize;

            ( LongFileWrite(fd, zero_cap, write, ZeroBlock, false) );

            zero_cap += write;
        }
    }

private:
    int           fd;
    uint_fast64_t alloc_cap;
    uint_fast64_t zero_cap;
};

class mmap_vector_base
{
public:
    explicit mmap_vector_base(mmap_storage& r)
        : size_real(0),
          size_reported(0),
          size_lazy_alloc(0),
          data(), rep(r),
          lock()
    {
    }

    ~mmap_vector_base()
    {
        for(size_t a=0; a<data.size(); ++a)
            data[a].map.Unmap();
    }

    unsigned char& GetRef(size_t offset, size_t size)
    {
      #if 0
        ScopedLock lck(lock);
        /* Maintain the lock during this entire function, because
         * EnsureRealSizeMatchesReportedSize() may cause data[]
         * to be reallocated, invalidating pointers to it
         */

        if(unlikely(offset+size > size_real))
        {
            EnsureRealSizeMatchesReportedSize();
        }
      #else
      { ScopedLock lck(lock);
        EnsureRealSizeMatchesReportedSize();
      }
      #endif

        typedef std::vector<MapData>::iterator it;
        it pos = std::lower_bound(data.begin(), data.end(), offset, MapOffsetComparator());

        if(pos != data.end() && data[0].offset == offset)
            {}
        else if(pos != data.begin())
            --pos;

        if(pos == data.end())
        {
            fprintf(stderr, "OUCH, offset %lu, size %lu is beyond limit pretend=%lu,intent=%lu,real=%lu\n",
                (unsigned long) offset, (unsigned long) size,
                (unsigned long) size_reported,
                (unsigned long) size_lazy_alloc,
                (unsigned long) size_real);
        }

        unsigned char* ptr =
            likely(pos->map)
                ? pos->map.get_write_ptr()
                : &pos->data[0];

        return ptr [ offset - pos->offset ];
    }

    void Resize(size_t newsize)
    {
        if(newsize < size_reported) return;

        size_reported = newsize;

        Reserve(size_reported);
    }

    void Reserve(size_t newsize)
    {
        if(newsize < size_lazy_alloc) return;

        size_lazy_alloc =
            (newsize + uint_fast64_t(MMAP_PAGESIZE-1)) & ~(MMAP_PAGESIZE-UINT64_C(1));
    }

    size_t get_size() const
    {
        return size_reported;
    }

    void clear()
    {
        for(size_t a=0; a<data.size(); ++a)
            data[a].map.Unmap();
        data.clear();
        size_reported   = 0;
        size_real       = 0;
        size_lazy_alloc = 0;
    }

    std::vector<unsigned char>& GetAndRelease()
    {
        EnsureRealSizeMatchesReportedSize();

        std::vector<unsigned char> replacement(size_real);
        /*
        fprintf(stderr, "Reported=%lu\n", replacement.size());
        for(size_t a=0; a<data.size(); ++a)
            fprintf(stderr, "Section %3u: %lu + %lu\n", (unsigned)a, data[a].offset, data[a].length);
        */
        for(size_t a=0; a<data.size(); ++a)
        {
            const unsigned char* ptr =
                likely(data[a].map)
                    ? data[a].map.get_ptr()
                    : &data[a].data[0];

            std::memcpy(&replacement[ data[a].offset ],
                        ptr,
                        data[a].length);
        }

        replacement.resize(size_reported);
        size_real       = size_reported;
        size_lazy_alloc = size_reported;

        for(size_t a=0; a<data.size(); ++a)
            data[a].map.Unmap();

        data.resize(1);
        data[0].offset = 0;
        data[0].length = size_reported;
        data[0].data.swap(replacement);

        return data[0].data;
    }
private:
    void EnsureRealSizeMatchesReportedSize()
    {
        if(unlikely(size_lazy_alloc > size_real))
        {
            if(!data.empty())
            {
                MapData& last = data.back();

                if(rep.TryAllocExtend(last.fileoffset, last.length, size_lazy_alloc - last.offset))
                {
                    // Just extend the mmapping.
                    uint_fast64_t oldsize = last.length;
                    last.length = size_lazy_alloc - last.offset;

                    if(last.map)
                    {
                        if(!last.map.ReMapIfNecessary(rep.GetFd(), last.fileoffset, last.length))
                        {
                            // remapping failed, make a vector
                            last.data.resize(last.length);
                            std::memcpy(&last.data[0],
                                        last.map.get_ptr(),
                                        oldsize);
                            last.map.Unmap();
                        }
                    }
                    else
                    {
                        last.data.resize(last.length);
                    }

                    size_real = size_lazy_alloc;
                    return;
                }
            }

            size_t        alloc_length = size_lazy_alloc-size_real;
            uint_fast64_t start        = rep.Alloc(alloc_length);

            MapData item;
            item.fileoffset = start;
            item.offset = size_real;
            item.length = alloc_length;
            /*
            fprintf(stderr, "Setting map (offset=%lu)... fd=%d, start=%lu, length=%lu\n",
                size_real,
                rep.GetFd(),
                start,
                alloc_length);
            */
            item.map.SetMap (rep.GetFd(), start, alloc_length, true);

            if(!item.map)
            {
                // mmapping failed! Create actual data buffer.
                item.data.resize(alloc_length);
                /*
                fprintf(stderr, "Through mapping fail, got %p..%p\n",
                    &item.data[0],
                    &item.data[0] + alloc_length);
                */
            }
            else
            {
                /*
                fprintf(stderr, "Through mapping, got %p..%p\n",
                    item.map.get_ptr(),
                    item.map.get_ptr() + alloc_length);
                */
            }

            data.push_back(item);

            size_real = size_lazy_alloc;
        }
    }

private:
    // copies are troublesome
    mmap_vector_base(const mmap_vector_base&);
    mmap_vector_base& operator=(const mmap_vector_base&);

private:
    size_t size_real;
    size_t size_reported;
    size_t size_lazy_alloc;

    struct MapData
    {
        uint_fast64_t offset; // offset from the beginning of the buffer
        uint_fast64_t length; // length of the mapping
        MemMappingType<false>      map;
        std::vector<unsigned char> data;
        uint_fast64_t fileoffset;

        MapData() : offset(),length(),map(),data(),fileoffset() { }
    };

    struct MapOffsetComparator
    {
        bool operator() (const MapData& b, const uint_fast64_t p) const
            { return b.offset < p; }
        bool operator() (const uint_fast64_t p, const MapData& b) const
            { return p < b.offset; }
    };

    std::vector<MapData> data;
    mmap_storage&        rep;

    MutexType            lock;
};

template<typename T>
class mmap_vector
{
public:
    explicit mmap_vector(mmap_storage& r)
        : backing(r)
    {
    }
    ~mmap_vector() { }

    inline void Resize(size_t n_items)
    {
        backing.Resize(n_items * sizeof(T));
    }

    inline void Reserve(size_t n_items)
    {
        backing.Reserve(n_items * sizeof(T));
    }

    std::vector<unsigned char>& GetAndRelease()
    {
        return backing.GetAndRelease();
    }

    void clear() { backing.clear(); }

    inline size_t size() const
    {
        return backing.get_size() / sizeof(T);
    }

    inline T& operator[] (size_t n)
    {
        unsigned char* ptr = &backing.GetRef( n * sizeof(T), sizeof(T) );
        return * (T*) ptr;
    }

    const inline T& operator[] (size_t n) const
    {
        mmap_vector_base& b = const_cast<mmap_vector_base&> (backing);

        const unsigned char* ptr = &b.GetRef( n * sizeof(T), sizeof(T) );
        return * (const T*) ptr;
    }

    void push_back(const T& item)
    {
        size_t end = size();
        Resize(end + 1);
        operator[] (end) = item;
    }

private:
    mmap_vector_base backing;
};

#endif
