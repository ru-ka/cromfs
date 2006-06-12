#include <cstdlib>

#include "fblock.hh"
#include "boyermoore.hh"
#include "mkcromfs_sets.hh"

const char* GetTempDir()
{
    const char* t;
    t = std::getenv("TEMP"); if(t) return t;
    t = std::getenv("TMP"); if(t) return t;
    return "/tmp";
}

int mkcromfs_fblock::compare_raw_portion(
    const unsigned char* data,
    uint_fast32_t data_size,
    uint_fast32_t my_offs)
{
    /* Notice: my_offs + data_size may be larger than the fblock size.
     * This can happen if there is a collision in the checksum index. A smaller
     * block might have been indexed, and it matches to a larger request.
     * We must check for that case, and reject if it is so.
     */

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
    
    if(!mmapped) Remap();
    if(mmapped)
    {
        return std::memcmp(mmapped + my_offs, &data[0], data_size);
    }
    
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
            close(fd);
            const unsigned char* pp = (const unsigned char*)p;
            result = std::memcmp(&data[0], pp + ignore, data_size);
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
}

void mkcromfs_fblock::get(std::vector<unsigned char>* raw,
                          std::vector<unsigned char>* compressed)
{
    if(!is_compressed && !mmapped) Remap();
    if(!is_compressed && mmapped)
    {
        if(raw) raw->assign(mmapped, mmapped+filesize);
        if(compressed)
            *compressed = LZMACompress(
                raw ? *raw
                    : std::vector<unsigned char> (mmapped, mmapped+filesize) );
        return;
    }
    
    int fd = open(getfn().c_str(), O_RDONLY | O_LARGEFILE);
    if(fd < 0)
    {
        static const std::vector<unsigned char> dummy;
        if(raw)        *raw = dummy;
        if(compressed) *compressed = dummy;
        return;
    }
    
    std::vector<unsigned char> result( filesize );
    
    read(fd, &result[0], result.size());
    close(fd);
    
    if(is_compressed)
    {
        if(compressed) *compressed = result;
        if(raw)        *raw = LZMADeCompress(result);
    }
    else
    {
        if(compressed)
        {
            *compressed = LZMACompress(result);
            put_compressed(*compressed);
        }
        if(raw)        *raw = result;
    }
}

void mkcromfs_fblock::put_appended_raw(
    const mkcromfs_fblock::AppendInfo& append,
    const unsigned char* data, const uint_fast32_t datasize)
{
    Unmap();

    const uint32_t cap = append.AppendBaseOffset + datasize;
    if(cap <= append.OldSize)
    {
        if(!is_compressed && append.OldSize != filesize)
        {
            throw "discrepancy";
        }
        /* File does not need to be changed */
        return;
    }

    if(cap != append.AppendedSize)
    {
        throw "discrepancy";
    }
    
    if(is_compressed)
    {
        std::vector<unsigned char> buf = get_raw();
        int fd = open(getfn().c_str(), O_RDWR | O_CREAT | O_LARGEFILE, 0644);
        if(fd < 0) { std::perror(getfn().c_str()); return; }

        is_compressed = false;
        if(!buf.empty()) write(fd, &buf[0], append.AppendBaseOffset);
        if(datasize > 0) write(fd, &data[0], datasize);
        
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

mkcromfs_fblock::AppendInfo
mkcromfs_fblock::AnalyzeAppend(
    const BoyerMooreNeedle& needle,
    uint_fast32_t minimum_pos)
{
    AppendInfo append;
    
    /***** Temporary buffer for the myriad of ways to handle the data *****/
    
    struct Buf
    {
        const unsigned char* Buffer;
    private:
        enum { None, Allocated } State;
    public:
        Buf() : Buffer(NULL), State(None) { }
        
        void AssignRefFrom(const unsigned char* d, unsigned)
        {
            Buffer = d; State = None;
        }
        void AssignCopyFrom(const unsigned char* d, unsigned size)
        {
            unsigned char* p = new unsigned char[size];
            std::memcpy(p, d, size);
            State = Allocated;
            Buffer = p;
        }
        void LoadFrom(int fd, uint_fast32_t size)
        {
            unsigned char* pp = new unsigned char[size];
            int res = pread(fd, pp, size, 0);
            Buffer = pp;
            State = Allocated;
        }
        ~Buf()
        {
            switch(State)
            {
                case Allocated: delete[] Buffer; break;
                case None: ;
            }
        }
    } Buffer;

    /***** Load file contents *****/
    
    if(is_compressed)
    {
        std::vector<unsigned char> rawdata = get_raw();
        append.OldSize = rawdata.size();
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
        append.OldSize = filesize;
        
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
            }
            else
            {
                Buffer.LoadFrom(fd, filesize);
                close(fd);
            }
        }
    }
    
    append.SetAppendPos(append.OldSize, needle.size());
    
    /**** Find the appension position ****/

#if DEBUG_APPEND
    std::fprintf(stderr, "Appension (%s), rawsize=%u, datasize=%u, ptr=%p, mmapped=%p\n",
        is_compressed ? "compressed" : "raw",
        append.OldSize, needle.size(),
        Buffer.Buffer,
        mmapped);
    std::fflush(stderr);
#endif
    if(!needle.empty() && append.OldSize > 0)
    {
        uint_fast32_t result = append.OldSize; /* By default, insert at end. */
        
        const unsigned char* ptr = Buffer.Buffer;
        
        /* The maximum offset where we can search for a complete match
         * using an optimized algorithm.
         */
        int_fast32_t full_match_max = (long)append.OldSize - (long)needle.size();
        if(full_match_max >= (int_fast32_t)minimum_pos) /* number of possible starting positions */
        {
            //std::fprintf(stderr, "full_match_max = %d\n", (int)full_match_max);
            
            /* +needle.size() because it is the number of bytes to search */
            uint_fast32_t res = minimum_pos + 
                needle.SearchIn(ptr + minimum_pos,
                                full_match_max + needle.size() - minimum_pos);
            if(res < full_match_max)
            {
                append.SetAppendPos(res, needle.size());
                return append;
            }
        }
        
        /* The rest of this algorithm checks for partial matches only */
        /* Though it _can_ check for complete matches too. */
        
        uint_fast32_t cap = std::min((long)append.OldSize, (long)(FSIZE - needle.size()));
        
        if(full_match_max < (int_fast32_t)minimum_pos) full_match_max = minimum_pos;
        for(uint_fast32_t a=full_match_max; a<cap; ++a)
        {
            /* We believe std::memchr() might be better optimized
             * than std::find(). At least in glibc, memchr() does
             * does longword access on aligned addresses, whereas
             * find() (from STL) compares byte by byte.
             */
            //fprintf(stderr, "a=%u, cap=%u\n", (unsigned)a, (unsigned)cap);
            const unsigned char* refptr =
                (const unsigned char*)std::memchr(ptr+a, needle[0], cap-a);
            if(!refptr) break;
            a = refptr - ptr;
            unsigned compare_size = std::min((long)needle.size(), (long)(append.OldSize - a));
            
            /* compare 1 byte less because find() already confirmed the first byte */
            if(std::memcmp(refptr+1, &needle[1], compare_size-1) == 0)
            {
#if DEBUG_OVERLAP
                std::printf("\nOVERLAP: ORIG=%u, NEW=%u, POS=%u, COMPARED %u\n",
                    (unsigned)cap, (unsigned)needle.size(),
                    a, compare_size);
                for(unsigned b=0; b<4+compare_size; ++b)
                    std::printf("%02X ", ptr[cap - compare_size+b-4]);
                std::printf("\n");
                for(unsigned b=0; b<4; ++b) std::printf("   ");
                for(unsigned b=0; b<4+compare_size; ++b)
                    std::printf("%02X ", needle[b]);
                std::printf("\n");
#endif
                result = a; /* Put it here. */
                break;
            }
        }
        
        append.SetAppendPos(result, needle.size());
    }
    return append;
}
