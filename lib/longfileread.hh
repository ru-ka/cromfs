#include "endian.hh"
#include "mmapping.hh"
#include "datareadbuf.hh"
#include "fadvise.hh"

#include <cerrno>

class LongFileRead
{
public:
    inline const unsigned char* GetAddr() const
        { return buf.Buffer; }
public:
    LongFileRead(int fd, uint_fast64_t pos, uint_fast64_t size)
        : buf(), map()
    {
        FadviseNoReuse(fd, pos, size);
        
        map.SetMap(fd, pos, size);
        if(map)
            buf.AssignRefFrom(map.get_ptr(), size);
        else
        {
            ssize_t r = buf.LoadFrom(fd, size, pos);
            if(r < 0) throw errno;
            if((size_t)r < size) throw EIO;
        }
    }

    LongFileRead(int fd, uint_fast64_t pos, uint_fast64_t size,
                 unsigned char* target)
        : buf(), map()
    {
        ssize_t r = pread64(fd, target, size, pos);
        if(r < 0) throw errno;
        if((size_t)r < size) throw EIO;
    }
private:
    DataReadBuffer buf;
    MemMapping map;

private:
    LongFileRead(const LongFileRead&);
    void operator=(const LongFileRead&);
};
