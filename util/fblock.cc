#include <cstdlib>

#include "fblock.hh"
#include "boyermoore.hh"
#include "mkcromfs_sets.hh"

#include "assert++.hh"

static const std::vector<unsigned char>
    DoLZMACompress(const std::vector<unsigned char>& data)
{
    if(LZMA_HeavyCompress == 2)
        return LZMACompressHeavy(data, "fblock");
    else if(LZMA_HeavyCompress)
        return LZMACompressAuto(data, "fblock");
    return LZMACompress(data);
}    

const char* GetTempDir()
{
    const char* t;
    t = std::getenv("TEMP"); if(t) return t;
    t = std::getenv("TMP"); if(t) return t;
    return "/tmp";
}

int fblock_storage::compare_raw_portion(
    const unsigned char* data,
    uint_fast32_t data_size,
    uint_fast32_t my_offs)
{
    /* Notice: my_offs + data_size may be larger than the fblock size.
     * This can happen if there is a collision in the checksum index. A smaller
     * block might have been indexed, and it matches to a larger request.
     * We must check for that case, and reject if it is so.
     */

#if 0
    /* This is neat and all, but has a shortcoming:
     * If data is uncompressed and mmap fails, InitDataReadBuffer will
     * pread() the entire file. But we only need to pread() the part
     * that we'll compare.
     */
    DataReadBuffer Buffer;
    uint_fast32_t oldsize;
    InitDataReadBuffer(Buffer, oldsize);
    if(oldsize < my_offs + data_size) return -1;
    return std::memcmp(&Buffer.Buffer[my_offs], &data[0], data_size);
#else
    if(is_compressed)
    {
        /* If the file is compressed, we must decompress it
         * to the RAM before it can be compared at all.
         * We now decompress it in its whole entirety.
         */
        std::vector<unsigned char> raw = get_raw();
        if(DecompressWhenLookup) put_raw(raw);
        ssize_t size      = data_size;
        ssize_t remaining = raw.size() - my_offs;
        if(remaining < size) return -1;
        return std::memcmp(&raw[my_offs], &data[0], size);
    }
    if(my_offs + data_size > filesize) return -1;
    
    /* Now the file is not compressed. */
    
    /* Try mmapping it (the entire file). */
    if(!mmapped) Remap();
    
    /* If mmapping immediately worked, the comparison
     * can be delegated to std::memcmp. */
    if(mmapped)
    {
        return std::memcmp(mmapped + my_offs, &data[0], data_size);
    }
    
    /* Don't give up with mmap yet. Try to mmap the specific portion
     * that we will be accessing now.
     */
    
    /* mmap only works when the starting my_offset is aligned
     * on a page boundary. Therefore, we force it to align.
     */
    uint_fast32_t prev_my_offs = my_offs & ~4095; /* 4095 is assumed to be page size-1 */
    /* Because of aligning, calculate the amount of bytes
     * that were mmapped but are not part of the comparison.
     */
    uint_fast32_t ignore = my_offs - prev_my_offs;
    
    int result = -1;
    int fd = open(getfn().c_str(), O_RDONLY | O_LARGEFILE);
    if(fd >= 0)
    {
        /* Try to use mmap. This way, only the portion of file
         * that actually needs to be compared, will be accessed.
         * If we are comparing an 1M block and memcmp detects a
         * difference within the first 3 bytes, only about 4 kB
         * of the file will be read. This is really fast.
         */
        void* p = mmap(NULL, ignore+data_size, PROT_READ, MAP_SHARED, fd, prev_my_offs);
        if(p != (void*)-1)
        {
            /* mmapping worked. close fd, since we only need the mmap now. */
            close(fd);
            
            /* Delegate comparison to std::memcmp */
            const unsigned char* pp = (const unsigned char*)p;
            result = std::memcmp(&data[0], pp + ignore, data_size);
            
            /* And unmap the temporary mapping */
            munmap(p, ignore+data_size);
        }
        else
        {
            /* If mmap didn't like our idea, try to use pread
             * instead. pread is llseek+read combined. This should
             * work if anything is going to work at all.
             */
            std::vector<unsigned char> tmpbuf(data_size);
            ssize_t r = pread(fd, &tmpbuf[0], data_size, my_offs);
            close(fd);
            if(r != (ssize_t)data_size)
                result = -1;
            else
                result = std::memcmp(&data[0], &tmpbuf[0], data_size);
        }
    }
    return result;
#endif
}

void fblock_storage::get(std::vector<unsigned char>* raw,
                         std::vector<unsigned char>* compressed)
{
    /* Is our data currently non-compressed? */
    if(!is_compressed)
    {
        /* Not compressed. */
        /* Ok. Ensure it's mmapped (mmapping is handy) */
        if(!mmapped) Remap();
        /* If mmapping worked, we're almost done. */
        if(mmapped)
        {
            /* If the caller wants raw, just copy it. */
            if(raw) raw->assign(mmapped, mmapped+filesize);
            
            /* If the caller wants compressed... */
            if(compressed)
            {
                *compressed = DoLZMACompress(
                    raw ? *raw /* If we already copied the raw data, use that */
                          /* Otherwise, create a vector for LZMACompress to use */
                        : std::vector<unsigned char> (mmapped, mmapped+filesize)
                                          );
            }
            return;
        }
        /* mmapping didn't work. Fortunately we have got a plan B. */
    }
    
    /* The data could not be mmapped. So we have to read a copy from the file. */
    int fd = open(getfn().c_str(), O_RDONLY | O_LARGEFILE);
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
            if(compressed)      *compressed = DoLZMACompress(*raw);
        }
        else if(compressed)
        {
            /* read into a temp buffer */
            std::vector<unsigned char> result( filesize );
            read(fd, &result[0], result.size());
            /* If the caller wants compressed, give a compressed copy. File remains raw. */
            *compressed = DoLZMACompress(result);
        }
    }
    close(fd);
}

void fblock_storage::put_appended_raw(
    const AppendInfo& append,
    const unsigned char* data,
    const uint_fast32_t datasize)
{
    Unmap();
    
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
        int fd = open(getfn().c_str(), O_RDWR | O_CREAT | O_LARGEFILE, 0644);
        if(fd < 0) { std::perror(getfn().c_str()); return; }

        is_compressed = false;
        
        assertvar(buf.size());
        assertvar(append.AppendBaseOffset);
        assert(buf.size() >= append.AppendBaseOffset);
        assertflush();

        if(!buf.empty()) write(fd, &buf[0], append.AppendBaseOffset);
        if(datasize > 0) write(fd, &data[0], datasize);
        
        /* Note: We need not worry about what comes after datasize.
         * If the block was fully submerged in the existing data,
         * we don't reach this far (already checked above).
         */
        
        filesize = cap;
        
        RemapFd(fd);
        
        close(fd);
        
        return;
    }
    
    /* not truncating */
    const uint32_t added_length = append.AppendedSize - append.OldSize;
    
    int fd = open(getfn().c_str(), O_RDWR | O_CREAT | O_LARGEFILE, 0644);
    if(fd < 0) { std::perror(getfn().c_str()); return; }
    if(pwrite(fd, &data[datasize - added_length], added_length, append.OldSize)  < 0)
    {
        fprintf(stderr, "pwrite failed - tried to write last %u from %p(size=%u) -- oldsize=%u, appendedsize=%u\n",
            (unsigned)added_length, &data[0], (unsigned)datasize,
            (unsigned)append.OldSize,
            (unsigned)append.AppendedSize
             );
        perror("pwrite");
    }
    ftruncate(fd, filesize = append.AppendedSize);
    RemapFd(fd);
    close(fd);
}

void fblock_storage::InitDataReadBuffer(DataReadBuffer& Buffer, uint_fast32_t& size)
{
    /* TODO: Add mechanism to load only a part of the file
     * (to be useful in compare_raw_portion() )
     */
    
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
                if(!mmapped) Remap();
                if(mmapped) goto UseMMapping;
            }
        }
        Buffer.AssignCopyFrom(&rawdata[0], rawdata.size());
    }
    else
    {
        size = filesize;
        
        if(!mmapped) Remap();
        if(mmapped)
        {
    UseMMapping:
            Buffer.AssignRefFrom(mmapped, filesize);
        }
        else
        {
            int fd = open(getfn().c_str(), O_RDWR | O_LARGEFILE);
            if(fd < 0)
            {
                /* File not found. Prevent null pointer, load a dummy buffer. */
                static const unsigned char d=0;
                Buffer.AssignRefFrom(&d, 0);
                size = 0; // not correct, but prevents segmentation fault...
            }
            else
            {
                Buffer.LoadFrom(fd, filesize);
                close(fd);
            }
        }
    }
}

/* Search the current fblock data for an occurance of the needle.
 * Returns an "AppendInfo" that describes how to append the needle
 * into the fblock, if it matched.
 */
const AppendInfo
fblock_storage::AnalyzeAppend(
    const BoyerMooreNeedle& needle,
    uint_fast32_t minimum_pos)
{
    /***** Temporary buffer for the myriad of ways to handle the data *****/
    /* With a destructor for exception-safety. */
    
    DataReadBuffer Buffer;
    uint_fast32_t oldsize;
    InitDataReadBuffer(Buffer, oldsize);
    return ::AnalyzeAppend(needle, minimum_pos, FSIZE, Buffer.Buffer, oldsize);
}
