#include "cromfs-fblockfun.hh"
#include "cromfs-blockindex.hh" // For EmergencyFreeSpace()

#include "../util/mkcromfs_sets.hh"

#include "boyermooreneedle.hh"
#include "longfilewrite.hh"
#include "longfileread.hh"
#include "assert++.hh"
#include "lzma.hh"

#include <cstdlib>
#include <sys/time.h>
#include <ctime>
#include <stdexcept>

using namespace fblock_private;

static const double FblockMaxAccessAgeBeforeDealloc = 10.0;

static std::string fblock_name_pattern;

void set_fblock_name_pattern(const std::string& pat)
{
    fblock_name_pattern = pat;
}

const std::string fblock_storage::getfn() const
{
    char Buf[4096];
    std::sprintf(Buf, "%d", fblock_disk_id);
    return fblock_name_pattern + Buf;
}

void fblock_storage::EnsureOpenFor(bool writing, bool create_if_absent)
{
    if(fd >= 0)
    {
        if(!(writing && !fd_writable))
            return;
        Close();
    }
    int flags = O_LARGEFILE
                | (writing ? O_RDWR : O_RDONLY)
                | (create_if_absent ? O_CREAT : 0);
retry:
    fd = open(getfn().c_str(), flags, 0644);
    if(fd < 0 && errno == ENFILE)
    {
        // Too many open files?
        // Ask kindly a few of our fellow fblocks to close their fds,
        // and try again.
        if(fblockset_global->CloseSome())
        {
            goto retry;
        }
    }
    fd_writable = writing;
}

bool fblock_storage::EnsureProperlyMapped()
{
    if(!mmapped)
    {
        if(fd < 0) return false;
        mmapped.SetMap(fd, 0, filesize);
    }
    else
    {
        mmapped.ReMapIfNecessary(fd, 0, filesize);
    }
    return mmapped;
}

void fblock_storage::Check_Existing_File()
{
    Unmap();

    EnsureOpenFor(false);
    if(fd < 0)
    {
        filesize = 0;
        is_compressed = false;
        return;
    }

    filesize = lseek64(fd, 0, SEEK_END);
    std::vector<unsigned char> Buffer(filesize);
    LongFileRead(fd, 0, filesize, &Buffer[0]);

    is_compressed = true;

    bool is_lzma_ok = false;
    try
    {
        if(filesize > 0)
            LZMADeCompress(Buffer, is_lzma_ok);
    }
    catch(std::bad_alloc)
    {
        // out of memory, probably invalid lzma data
        is_lzma_ok = false;
    }

    if(!is_lzma_ok)
    {
        is_compressed = false;
    }
}

void fblock_storage::put_raw(const std::vector<unsigned char>& raw)
{
    Unmap();

    is_compressed = false;

    EnsureOpenFor(true, true);

    try
    {
        ( LongFileWrite(fd, 0, filesize=raw.size(), &raw[0], false) );
        ftruncate(fd, filesize);
    }
    catch(int err)
    {
        std::fprintf(stderr, "pwrite(%s,decompressed): Possible trouble -- trying to write compressed version to save space\n",
            getfn().c_str());
        if(errno) perror("pwrite");

        // Possibly, out of disk space? Try to save compressed instead.
        put_compressed(LZMACompress(raw));
    }
    last_access = std::time(NULL);
}

void fblock_storage::put_compressed(const std::vector<unsigned char>& compressed)
{
    Unmap();

    //fprintf(stderr, "[1;mstoring compressed[m\n");
    is_compressed = true;

TryAgain:;

    EnsureOpenFor(true, true);

    try
    {
        ( LongFileWrite(fd, 0, filesize=compressed.size(), &compressed[0], false) );
        ftruncate(fd, compressed.size());
    }
    catch(int err)
    {
        std::fprintf(stderr, "fwrite(%s,compressed): Trouble writing %d bytes\n",
            getfn().c_str(), (int)filesize);

        if(block_index_global->EmergencyFreeSpace())
        {
            fprintf(stderr, "Trying fwrite again\n");
            goto TryAgain;
        }
        std::fprintf(stderr, "fwrite(%s,compressed): Fatal error\n", getfn().c_str());
        throw std::runtime_error("Cannot write a fblock"); // FATAL ERROR
    }
    last_access = std::time(NULL);
}

void fblock_storage::get(std::vector<unsigned char>* raw,
                         std::vector<unsigned char>* compressed)
{
    last_access = std::time(NULL);

    /* Is our data currently non-compressed? */
    if(!is_compressed)
    {
        /* Not compressed. */
        /* Ok. Ensure it's mmapped (mmapping is handy) */
        if(EnsureProperlyMapped())
        {
            /* If mmapping worked, we're almost done. */
            /* If the caller wants raw, just copy it. */
            if(raw) raw->assign(mmapped.get_ptr(), mmapped.get_ptr()+filesize);

            /* If the caller wants compressed... */
            if(compressed)
            {
                *compressed = DoLZMACompress(LZMA_HeavyCompress,
                    raw ? *raw /* If we already copied the raw data, use that */
                          /* Otherwise, create a vector for LZMACompress to use */
                        : std::vector<unsigned char> (mmapped.get_ptr(), mmapped.get_ptr()+filesize)
                                          , "fblock");
            }
            return;
        }
        /* mmapping didn't work. Fortunately we have got a plan B. */
    }

    /* The data could not be mmapped. So we have to read a copy from the file. */
    EnsureOpenFor(false);

    if(fd < 0)
    {
        /* If the file could not be opened, return dummy vectors. */
        static const std::vector<unsigned char> dummy;
        if(raw)        *raw = dummy;
        if(compressed) *compressed = dummy;
        return;
    }

    /* Read the file contents */

    /* Is the file data compressed? */
    if(is_compressed)
    {
        /* File is compressed */
        if(compressed) /* does the caller want compressed data? */
        {
            /* read into the desired compressed data */
            compressed->resize(filesize);
            read(fd, &(*compressed)[0], (*compressed).size());
            /* If the caller wants raw, give a decompressed copy. File remains compressed. */
            if(raw)      *raw = LZMADeCompress(*compressed);
        }
        else if(raw)
        {
            /* read into a temp buffer */
            std::vector<unsigned char> result( filesize );
            read(fd, &result[0], result.size());
            /* If the caller wants raw, give a decompressed copy. File remains compressed. */
            *raw = LZMADeCompress(result);
        }
    }
    else
    {
        /* File is uncompressed (raw) */
        if(raw) /* does the caller want raw data? */
        {
            /* read into the desired raw data */
            raw->resize(filesize);
            read(fd, &(*raw)[0], (*raw).size());
            /* If the caller wants compressed, give a compressed copy. File remains raw. */
            if(compressed)      *compressed = DoLZMACompress(LZMA_HeavyCompress, *raw, "fblock");
        }
        else if(compressed)
        {
            /* read into a temp buffer */
            std::vector<unsigned char> result( filesize );
            read(fd, &result[0], result.size());
            /* If the caller wants compressed, give a compressed copy. File remains raw. */
            *compressed = DoLZMACompress(LZMA_HeavyCompress, result, "fblock");
        }
    }

    // fd will be automatically closed.
}

void fblock_storage::put_appended_raw(
    const AppendInfo& append,
    const unsigned char* data,
    const uint_fast32_t datasize)
{
    last_access = std::time(NULL);

    const uint32_t cap = append.AppendBaseOffset + datasize;
    if(cap <= append.OldSize)
    {
        assertvar(is_compressed),
        assertvar(append.OldSize);
        assertvar(filesize);
        assert(is_compressed || append.OldSize == filesize);
        assertflush();

        /* File does not need to be changed */
        return;
    }

    assertvar(cap);
    assertvar(append.AppendedSize);
    assert(cap == append.AppendedSize);
    assertflush();

    if(is_compressed)
    {
        /* We cannot append into compressed data. Must decompress it first. */

        std::vector<unsigned char> buf = get_raw();
        EnsureOpenFor(true);

        if(fd < 0)
        {
            std::perror(getfn().c_str());
            throw std::runtime_error("Cannot open a fblock"); // FATAL ERROR
        }

        is_compressed = false;

        assertvar(buf.size());
        assertvar(append.AppendBaseOffset);
        assert(buf.size() >= append.AppendBaseOffset);
        assertflush();

        // Write preceding part:
        if(!buf.empty()) write(fd, &buf[0], append.AppendBaseOffset);
        // Write appended part:
        if(datasize > 0) write(fd, &data[0], datasize);

        /* Note: We need not worry about what comes after datasize.
         * If the block was fully submerged in the existing data,
         * we don't reach this far (already checked above).
         */

        filesize = cap;

        return;
    }

    /* not truncating */
    const uint32_t added_length = append.AppendedSize - append.OldSize;

    EnsureOpenFor(true, append.OldSize == 0);

    if(fd < 0)
    {
        std::perror(getfn().c_str());
        throw std::runtime_error("Cannot open a fblock"); // FATAL ERROR
    }
    errno = 0;

TryAgain:
    try
    {
        ( LongFileWrite(fd, append.OldSize, added_length, &data[datasize - added_length],
                        false) );
    }
    catch(int err)
    {
        fprintf(stderr, "pwrite failed - tried to write last %u from %p(size=%u) -- oldsize=%u, appendedsize=%u\n",
            (unsigned)added_length, &data[0], (unsigned)datasize,
            (unsigned)append.OldSize,
            (unsigned)append.AppendedSize
             );
        if(errno) perror("pwrite");

        if(block_index_global->EmergencyFreeSpace())
        {
            fprintf(stderr, "Trying pwrite again\n");
            goto TryAgain;
        }
        throw std::runtime_error("Cannot append to a fblock"); // FATAL ERROR
    }
    filesize = append.AppendedSize;
    //ftruncate(fd, filesize); //-- what use is this for?
}

void fblock_storage::InitDataReadBuffer(DataReadBuffer& Buffer, uint_fast32_t& size)
{
    InitDataReadBuffer(Buffer, size, 0, uint_fast32_t(~0UL));
}

void fblock_storage::InitDataReadBuffer(
    DataReadBuffer& Buffer, uint_fast32_t& size,
    uint_fast32_t req_offset,
    uint_fast32_t req_size)
{
    /***** Load file contents *****/
    size = 0;
    if(is_compressed)
    {
        std::vector<unsigned char> rawdata = get_raw();
        size = rawdata.size();
        if(DecompressWhenLookup)
        {
            put_raw(rawdata);
            if(!is_compressed)
            {
                if(EnsureProperlyMapped())
                    goto UseMMapping;
            }
        }

        if(req_offset > size) req_offset = size;

        //fprintf(stderr, "Using copy of compressed\n"); fflush(stderr);

        Buffer.AssignCopyFrom(&rawdata[req_offset],
            std::min(size - req_offset, req_size)
                             );
    }
    else
    {
        size = filesize;

        if(req_offset > size) req_offset = size;

        if(EnsureProperlyMapped())
        {
    UseMMapping:
            //fprintf(stderr, "Using mmap\n"); fflush(stderr);
            Buffer.AssignRefFrom(mmapped.get_ptr() + req_offset,
                std::min(size - req_offset, req_size)
                                );
        }
        else
        {
            //fprintf(stderr, "Has to read\n"); fflush(stderr);

            EnsureOpenFor(true);

            if(fd < 0)
            {
                /* File not found. Prevent null pointer, load a dummy buffer. */
                static const unsigned char d=0;
                Buffer.AssignRefFrom(&d, 0);
                size = 0; // not correct, but prevents segmentation fault...
            }
            else
            {
                Buffer.LoadFrom(fd,
                    std::min(size - req_offset, req_size),
                                req_offset);
            }
            // fd will be automatically closed.
        }
    }
    last_access = std::time(NULL);
}

mkcromfs_fblockset::~mkcromfs_fblockset()
{
    for(unsigned a=0; a<fblocks.size(); ++a)
    {
        fblocks[a].Delete();
    }
}

mkcromfs_fblock& mkcromfs_fblockset::operator[] (size_t index)
{
    if(index >= size())
        fblocks.resize(index+1);

    return fblocks[index];
}

int mkcromfs_fblockset::index_type::FindAtleastNbytesSpace(size_t howmuch) const
{
    room_index_t::const_iterator i = room_index.lower_bound(howmuch);
    if(i != room_index.end())
        return i->second;
    return -1;
}

void mkcromfs_fblockset::index_type::Update(cromfs_fblocknum_t index, size_t howmuch)
{
    if(1) // scope
    {
        /* Erase previous data if exists */
        block_index_t::iterator i = block_index.find(index);
        if(i != block_index.end())
        {
            room_index_t::iterator& j = i->second;
            if(j->first == howmuch) return; // no change
            room_index.erase(j);
            block_index.erase(i);
        }
    }
    if(howmuch > 0)
    {
        room_index_t::iterator i = room_index.insert( std::make_pair(howmuch, index) );
        block_index[index] = i;
    }
}

mkcromfs_fblockset::undo_t mkcromfs_fblockset::create_backup() const
{
    undo_t e;
    e.n_fblocks = fblocks.size();
    e.fblock_state.reserve(fblocks.size());
    for(unsigned a = 0; a < fblocks.size(); ++a)
        e.fblock_state.push_back(fblocks[a].create_backup());
    e.fblock_index = index;
    return e;
}

void mkcromfs_fblockset::restore_backup(const undo_t& e)
{
    for(size_t a = e.n_fblocks; a<fblocks.size(); ++a)
        fblocks[a].Delete();

    fblocks.resize(e.n_fblocks);

    for(size_t a = 0; a < e.fblock_state.size(); ++a)
        fblocks[a].restore_backup(e.fblock_state[a]);
    index = e.fblock_index;
}

class OrderByAccessTime
{
private:
    const std::vector<mkcromfs_fblock>& fblocks;
public:
    OrderByAccessTime(const std::vector<mkcromfs_fblock>& f) : fblocks(f) { }

    bool operator() (size_t a, size_t b) const
    {
        return fblocks[a].get_last_access()
             < fblocks[b].get_last_access();
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
        mkcromfs_fblock& fblock = fblocks[fblocknums[a]];

        time_t last_access = fblock.get_last_access();

        if(std::difftime(now, last_access) >= FblockMaxAccessAgeBeforeDealloc)
        {
            if(do_randomcompress && fblock.is_uncompressed())
            {
                fblock.put_compressed(LZMACompress(fblock.get_raw()));
                do_randomcompress = false;

                fblock.Close();
                continue;
            }

            fblock.Unmap();
            fblock.Close();
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
    {
        mkcromfs_fblock& fblock = fblocks[fblocknums[a]];
        if(fblock.Close())
            return true;
    }
    return false;
}

mkcromfs_fblockset* fblockset_global = 0;
