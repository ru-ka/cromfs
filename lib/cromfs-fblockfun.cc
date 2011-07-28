#include "cromfs-fblockfun.hh"
#include "../util/mkcromfs_sets.hh"
#include "datareadbuf.hh"
#include "longfilewrite.hh"
#include "longfileread.hh"
#include "lzma.hh"

#include <algorithm>
#include <errno.h>
#include <ctime>

static const double FblockMaxAccessAgeBeforeDealloc = 10.0;

static std::string fblock_name_pattern;
void set_fblock_name_pattern(const std::string& pat)
{
    fblock_name_pattern = pat;
}


///////////////////////

mkcromfs_fblockset* fblockset_global = 0;

///////////////////////

mkcromfs_fblockset::mkcromfs_fblockset()
    : fblocks()
{
}
mkcromfs_fblockset::~mkcromfs_fblockset()
{
    for(size_t a=0; a<fblocks.size(); ++a)
        delete fblocks[a].ptr;
}

mkcromfs_fblock& mkcromfs_fblockset::operator[] (size_t index)
{
    if(index >= fblocks.size())
    {
        size_t oldsize = fblocks.size();
        fblocks.resize(index+1);
        while(oldsize < fblocks.size())
        {
            fblocks[oldsize].ptr         = new mkcromfs_fblock(oldsize);
            fblocks[oldsize].last_access = std::time(0);
            fblocks[oldsize].space       = 0;
            ++oldsize;
        }
    }
    fblocks[index].last_access = std::time(0);
    return *fblocks[index].ptr;
}

const mkcromfs_fblock& mkcromfs_fblockset::operator[] (size_t index) const
{
    (const_cast<fblock_rec&> (fblocks[index])).last_access = std::time(0);
    return *fblocks[index].ptr;
}

int mkcromfs_fblockset::FindFblockThatHasAtleastNbytesSpace(size_t howmuch) const
{
    bool first = true; size_t tightest = 0;
    for(size_t a=0; a<fblocks.size(); ++a)
        if(fblocks[a].space >= howmuch
        && (first || fblocks[a].space < fblocks[tightest].space)
          )
        {
            tightest = a;
            first    = false;
        }
    return first ? -1 : (int)tightest;
}

void mkcromfs_fblockset::UpdateFreeSpaceIndex(cromfs_fblocknum_t fnum, size_t howmuch)
{
    fblocks[fnum].space = howmuch;
}

//////////////////////

class OrderByAccessTime
{
private:
    const std::vector<mkcromfs_fblockset::fblock_rec>& fblocks;
public:
    OrderByAccessTime(const std::vector<mkcromfs_fblockset::fblock_rec>& f) : fblocks(f) { }

    bool operator() (size_t a, size_t b) const
    {
        return fblocks[a].last_access
             < fblocks[b].last_access;
    }
};

void mkcromfs_fblockset::FreeSomeResources()
{
    static unsigned counter = RandomCompressPeriod;
    bool do_randomcompress = !counter;
    if(do_randomcompress) counter = RandomCompressPeriod; else { --counter; }

    std::vector<size_t> fblocknums(fblocks.size());
    for(size_t a=0; a<fblocks.size(); ++a)
        fblocknums[a]=a;
    std::sort(fblocknums.begin(), fblocknums.end(), OrderByAccessTime(fblocks) );

    time_t now = std::time(NULL);
    for(size_t a=0; a<fblocks.size(); ++a)
    {
        fblock_rec& fblock = fblocks[fblocknums[a]];

        time_t last_access = fblock.last_access;

        if(std::difftime(now, last_access) >= FblockMaxAccessAgeBeforeDealloc)
        {
            if(do_randomcompress && fblock.ptr->is_uncompressed())
            {
                fblock.ptr->Compress();
                do_randomcompress = false;
            }
            fblock.ptr->Unmap();
            fblock.ptr->Close();
        }
    }

    if(do_randomcompress)
        counter = 0; /* postpone it if we cannot comply */
}

bool mkcromfs_fblockset::CloseSome()
{
    std::vector<size_t> fblocknums(fblocks.size());
    for(size_t a=0; a<fblocks.size(); ++a)
        fblocknums[a]=a;
    std::sort(fblocknums.begin(), fblocknums.end(), OrderByAccessTime(fblocks) );

    for(size_t a=0; a<fblocks.size(); ++a)
        if(fblocks[fblocknums[a]].ptr->Close())
            return true;
    return false;
}

///////////////////////

std::string mkcromfs_fblock::getfn() const
{
    char Buf[4096];
    std::sprintf(Buf, "%d", fblock_disk_id);
    return fblock_name_pattern + Buf;
}

mkcromfs_fblock::mkcromfs_fblock(int id)
    : lock(),
      fblock_disk_id(id), filesize(0), mapped(), fd(-1), is_compressed(false)
{
}

mkcromfs_fblock::~mkcromfs_fblock()
{
    Delete();
}

void mkcromfs_fblock::Delete()
{
    Unmap();
    Close();
    unlink(getfn().c_str());
}

void mkcromfs_fblock::EnsureMMapped(bool decompressed) const
{
    (const_cast<mkcromfs_fblock*>(this))->EnsureMMapped(decompressed);
}

void mkcromfs_fblock::EnsureMMapped(bool want_decompressed)
{
    if(is_compressed && want_decompressed) Decompress();
    if(!mapped)
    {
        EnsureOpen();
        mapped.SetMap(fd, 0, filesize, true);
        if(mapped) Close();
    }
}

bool mkcromfs_fblock::Close()
{
    if(fd < 0) return false;
    close(fd);
    fd = -1;
    return true;
}

void mkcromfs_fblock::EnsureOpen()
{
    if(fd < 0)
    {
retry:
        fd = open(getfn().c_str(), O_RDWR | O_LARGEFILE | O_CREAT, 0644);
        // TODO: error resolution
        if(fd < 0 && (errno == ENFILE || errno == EMFILE))
        {
            // Too many open files?
            // Ask kindly a few of our fellow fblocks to close their fds,
            // and try again.
            if(fblockset_global->CloseSome())
            {
                goto retry;
            }
        }
        // Do not mmap it here.
    }
}

void mkcromfs_fblock::
    InitDataReadBuffer(DataReadBuffer& Buffer, uint_fast32_t& size,
                       uint_fast32_t req_offset,
                       uint_fast32_t req_size) const
{
    (const_cast<mkcromfs_fblock*>(this))
        ->InitDataReadBuffer(Buffer,size,req_offset,req_size);
}

void mkcromfs_fblock::
    InitDataReadBuffer(DataReadBuffer& Buffer, uint_fast32_t& size,
                       uint_fast32_t req_offset,
                       uint_fast32_t req_size)
{
    if(DecompressWhenLookup || !is_compressed)
    {
        EnsureMMapped(); // also decompresses.
        size = filesize;

        uint_fast32_t extent = filesize;
        if(req_offset > extent) req_offset = extent;
        if(req_offset + req_size > extent
                     || req_size > extent)
        {
            req_size = extent - req_offset;
            if(req_size == 0) req_offset = 0;
        }
        if(mapped)
            Buffer.AssignRefFrom(mapped.get_ptr() + req_offset, req_size);
        else
        {
            EnsureOpen();
            Buffer.LoadFrom(fd, req_size, req_offset);
        }
    }
    else // it's compressed and we must not save it decompressed.
    {
        EnsureOpen();

        if(mapped)
        {
            DataReadBuffer rdbuf;
            rdbuf.AssignRefFrom(mapped.get_ptr(), filesize);
            std::vector<unsigned char> decompressed = LZMADeCompress(rdbuf.Buffer, filesize);

            uint_fast32_t extent = decompressed.size();
            if(req_offset > extent) req_offset = extent;
            if(req_offset + req_size > extent
                         || req_size > extent)
            {
                req_size = extent - req_offset;
                if(req_size == 0) req_offset = 0;
            }

            Buffer.AssignCopyFrom(&decompressed[req_offset], req_size);
        }
        else
        {
            EnsureOpen();
            LongFileRead rdr(fd, 0, filesize);
            std::vector<unsigned char> decompressed = LZMADeCompress(rdr.GetAddr(), filesize);

            uint_fast32_t extent = decompressed.size();
            if(req_offset > extent) req_offset = extent;
            if(req_offset + req_size > extent
                         || req_size > extent)
            {
                req_size = extent - req_offset;
                if(req_size == 0) req_offset = 0;
            }

            Buffer.AssignCopyFrom(&decompressed[req_offset], req_size);
        }
    }
}

void mkcromfs_fblock::InitCompressedDataReadBuffer(DataReadBuffer& Buffer, uint_fast32_t& size)
{
    if(!is_compressed)
    {
        DataReadBuffer buf; //uint_fast32_t size;
        InitDataReadBuffer(buf, size);
        std::vector<unsigned char> compressed = DoLZMACompress(LZMA_HeavyCompress,
            buf.Buffer, size,
            "fblock");
        put_compressed(compressed);

        // Try map it
        EnsureMMapped(false);
        if(mapped)
            Buffer.AssignRefFrom(mapped.get_ptr(), size = filesize);
        else
            Buffer.AssignCopyFrom(&compressed[0], size = filesize);
        return;
    }
    // It's compressed.
    EnsureMMapped(false);
    if(!mapped)
    {
        // Failed to map it.
        Buffer.LoadFrom(fd, size = filesize, 0);
    }
    // It's compressed and mapped.
    Buffer.AssignRefFrom(mapped.get_ptr(), size = filesize);
}

void mkcromfs_fblock::SetFileContent(const unsigned char* ptr, uint_fast32_t length)
{
    Unmap();
    EnsureOpen();
#if 1
    /* Disable this function until it can be tested properly */
    if(filesize >= length)
    {
        // There's enough backing storage for writing the updated contents into the file
        if(mapped)
            if(!mapped.ReMapIfNecessary(fd, 0, filesize=length))
                Unmap();

        if(!mapped)
            mapped.SetMap(fd, 0, filesize = length, true);

        if(mapped)
        {
            std::memcpy(mapped.get_write_ptr(), ptr, length);
            mapped.Sync();
            ftruncate(fd, filesize);
            Close();
            return;
        }
    }
#endif
    ( LongFileWrite(fd, 0, filesize=length, ptr, false) );
    ftruncate(fd, filesize);
}

void mkcromfs_fblock::Decompress()
{
    if(is_compressed)
    {
        if(mapped)
        {
            DataReadBuffer rdbuf;
            rdbuf.AssignRefFrom(mapped.get_ptr(), filesize);
            std::vector<unsigned char> decompressed = LZMADeCompress(rdbuf.Buffer, filesize);
            Unmap();
            SetFileContent(&decompressed[0], decompressed.size());
        }
        else
        {
            EnsureOpen();
            LongFileRead rdr(fd, 0, filesize);
            std::vector<unsigned char> decompressed = LZMADeCompress(rdr.GetAddr(), filesize);
            SetFileContent(&decompressed[0], decompressed.size());
        }
        is_compressed = false;
    }
}

std::vector<unsigned char> mkcromfs_fblock::get_compressed()
{
    if(!is_compressed)
    {
        DataReadBuffer buf; uint_fast32_t size;
        InitDataReadBuffer(buf, size);
        std::vector<unsigned char> compressed = DoLZMACompress(LZMA_HeavyCompress,
            buf.Buffer, size,
            "fblock");
        put_compressed(compressed);
        return compressed;
    }

    if(mapped)
        return std::vector<unsigned char> (mapped.get_ptr(), mapped.get_ptr()+filesize);
    EnsureOpen();
    LongFileRead rdr(fd, 0, filesize);
    return std::vector<unsigned char> (rdr.GetAddr(), rdr.GetAddr()+filesize);
}

void mkcromfs_fblock::Compress()
{
    if(!is_compressed)
    {
        DataReadBuffer buf; uint_fast32_t size;
        InitDataReadBuffer(buf, size);
        std::vector<unsigned char> compressed = DoLZMACompress(LZMA_HeavyCompress,
            buf.Buffer, size,
            "fblock");
        put_compressed(compressed);
    }
}

void mkcromfs_fblock::Unmap()
{
    mapped.Unmap();
}

void mkcromfs_fblock::put_compressed(const std::vector<unsigned char>& data)
{
    is_compressed = true;
    SetFileContent(&data[0], data.size());
}

void mkcromfs_fblock::put_appended_raw(
    const AppendInfo& append,
    const unsigned char* data,
    size_t               length)
{
    Decompress(); // ensure it's not compressed

    if(append.AppendBaseOffset + length <= filesize)
    {
        // It sinks completely into the file.
        // No need to write.
        return;
    }

    EnsureOpen(); // ensure we can write to it

    size_t amount_added        = append.AppendBaseOffset + length - filesize;
    size_t amount_alreadythere = length - amount_added;

    ( LongFileWrite(fd,
                    append.AppendBaseOffset + amount_alreadythere,
                    amount_added,
                    data + amount_alreadythere,
                    false) );

    if(append.AppendBaseOffset + length > filesize)
    {
        filesize = append.AppendBaseOffset + length;
    }

    if(mapped)
    {
        // Ensure the memory mapping covers the full size of the file.
        if(!mapped.ReMapIfNecessary(fd, 0, filesize))
        {
            // Remapping failed, unmap the file.
            mapped.Unmap();
        }
    }
}
