#define DEBUG_APPEND  0
#define DEBUG_OVERLAP 0
#define DEBUG_FBLOCKINDEX 0

#include <vector>
#include <string>
#include <cstdio>
#include <cstdlib>
#include <stdint.h>
#include <sys/mman.h>
#include <unistd.h>
#include <fcntl.h>

#include "memmem.h"

const char* GetTempDir();

class mkcromfs_fblock
{
private:
    int fblock_disk_id;
    bool is_compressed;
    uint_fast32_t filesize;
    unsigned char* mmapped;
public:
    mkcromfs_fblock()
    {
        static int disk_id = 0;
        fblock_disk_id = disk_id++;
        filesize = 0;
        is_compressed = false;
        mmapped = NULL;
    }
    
    bool is_uncompressed() const { return !is_compressed; }
    
    const std::string getfn() const
    {
        static const std::string tmpdir = GetTempDir();
        static const int pid = getpid();
        char Buf[4096];
        std::sprintf(Buf, "/fblock_%d-%d", pid, fblock_disk_id);
        //fprintf(stderr, "Buf='%s' tmpdir='%s'\n", Buf, tmpdir.c_str());
        return tmpdir + Buf;
    }
    
    void Delete()
    {
        Unmap();
        ::unlink(getfn().c_str());
        filesize = 0;
    }
    
    void get(std::vector<unsigned char>& raw,
             std::vector<unsigned char>& compressed)
    {
        get(&raw, &compressed);
    }

    const std::vector<unsigned char> get_raw()
    {
        std::vector<unsigned char> raw;
        get(&raw, NULL);
        return raw;
    }

    const std::vector<unsigned char> get_compressed()
    {
        std::vector<unsigned char> compressed;
        get(NULL, &compressed);
        return compressed;
    }
    
    int compare_raw_portion(const std::vector<unsigned char>& data, uint_fast32_t offs)
    {
        /* Notice: offs + data.size() may be larger than the fblock size.
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

            ssize_t size      = data.size();
            ssize_t remaining = raw.size() - offs;
            if(remaining < size) return -1;
            return std::memcmp(&raw[offs], &data[0], size);
        }
        
        if(offs + data.size() > filesize) return -1;
        
        if(!mmapped) Remap();
        if(mmapped)
        {
            return std::memcmp(mmapped + offs, &data[0], data.size());
        }
        
        /* mmap only works when the starting offset is aligned
         * on a page boundary. Therefore, we force it to align.
         */
        uint_fast32_t prev_offs = offs & ~4095; /* 4095 is assumed to be page size-1 */
        /* Because of aligning, calculate the amount of bytes
         * that were mmapped but are not part of the comparison.
         */
        uint_fast32_t ignore = offs - prev_offs;
        
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
            void* p = mmap(NULL, ignore+data.size(), PROT_READ, MAP_SHARED, fd, prev_offs);
            if(p != (void*)-1)
            {
                close(fd);
                const char* pp = (const char*)p;
                result = std::memcmp(&data[0], pp + ignore, data.size());
                munmap(p, ignore+data.size());
            }
            else
            {
                /* If mmap didn't like our idea, try to use pread
                 * instead. pread is llseek+read combined. This should
                 * work if anything is going to work at all.
                 */
                std::vector<unsigned char> tmpbuf(data.size());
                ssize_t r = pread(fd, &tmpbuf[0], data.size(), offs);
                close(fd);
                if(r != (ssize_t)data.size())
                    result = -1;
                else
                    result = std::memcmp(&data[0], &tmpbuf[0], data.size());
            }
        }
        return result;
    }
    
    void put_raw(const std::vector<unsigned char>& raw)
    {
        Unmap();
        
        is_compressed = false;
        FILE* fp = std::fopen(getfn().c_str(), "wb");
        size_t res = std::fwrite(&raw[0], 1, filesize=raw.size(), fp);
        std::fclose(fp);
        if(res != raw.size())
        {
            std::fprintf(stderr, "fwrite: res=%d, should be %d\n", (int)res, (int)raw.size());
            // Possibly, out of disk space? Try to save compressed instead.
            put_compressed(LZMACompress(raw));
        }
        /* Remap(); */
    }
    
    void put_compressed(const std::vector<unsigned char>& compressed)
    {
        Unmap();
        
        //fprintf(stderr, "[1;mstoring compressed[m\n");
        is_compressed = true;
        FILE* fp = std::fopen(getfn().c_str(), "wb");
        std::fwrite(&compressed[0], 1, filesize=compressed.size(), fp);
        std::fclose(fp);
    }

    void put(const std::vector<unsigned char>& raw,
             const std::vector<unsigned char>& compressed)
    {
        /* This method can choose freely whether to store
         * in compressed or uncompressed format. We choose
         * compressed, because recompression would take a
         * lot of time, but decompression is fast.
         */
        
        put_compressed(compressed);
        /*
        if(is_compressed)
            put_compressed(compressed);
        else
            put_raw(raw);
        */
    }
    
    /* AppendInfo is a structure that holds both the input and output
     * handled by LoadRawAndAppend(). It was created to avoid having
     * to copy and resize std::vectors everywhere. It was supposed to
     * use mmap() to minimize the file access.
     */
    struct AppendInfo
    {
        uint_fast32_t OldSize;
        uint_fast32_t AppendBaseOffset;
        uint_fast32_t AppendedSize;
    public:
        AppendInfo() : OldSize(0), AppendedSize(0) { }
        void SetAppendPos(uint_fast32_t offs, uint_fast32_t datasize)
        {
            AppendBaseOffset = offs;
            AppendedSize     = std::max(OldSize, offs + datasize);
        }
    };
    
    /* Load file contents, analyze how to append/overlap but don't do it */
    AppendInfo AnalyzeAppend(const std::vector<unsigned char>& data)
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
            Buffer.AssignCopyFrom(&rawdata[0], rawdata.size());
        }
        else
        {
            append.OldSize = filesize;
            
            if(!mmapped) Remap();
            if(mmapped)
            {
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
        
        append.SetAppendPos(append.OldSize, data.size());
        
        /**** Find the appension position ****/

#if DEBUG_APPEND
        std::fprintf(stderr, "Appension (%s), rawsize=%u, datasize=%u, ptr=%p, mmapped=%p\n",
            is_compressed ? "compressed" : "raw",
            append.OldSize, data.size(),
            Buffer.Buffer,
            mmapped);
        std::fflush(stderr);
#endif
        if(!data.empty() && append.OldSize > 0)
        {
            uint_fast32_t result = append.OldSize; /* By default, insert at end. */
            
            const unsigned char* ptr = Buffer.Buffer;
            
            /* The maximum offset where we can search for a complete match
             * using an optimized algorithm.
             */
            int_fast32_t full_match_max = std::max(0L, (long)(append.OldSize - data.size()));
            if(full_match_max > 0)
            {
                uint_fast32_t res = fast_memmem(ptr, full_match_max, &data[0], data.size());
                if(res < full_match_max)
                {
                    append.SetAppendPos(res, data.size());
                    return append;
                }
            }
            
            /* The rest of this algorithm checks for partial matches only */
            /* Though it _can_ check for complete matches too. */
            
            uint_fast32_t cap = std::min((long)append.OldSize, (long)(FSIZE - data.size()));

            for(unsigned a=full_match_max; a<cap; ++a)
            {
                /* We believe std::memchr() might be better optimized
                 * than std::find(). At least in glibc, memchr() does
                 * does longword access on aligned addresses, whereas
                 * find() (from STL) compares byte by byte.
                 */
                //fprintf(stderr, "a=%u, cap=%u\n", (unsigned)a, (unsigned)cap);
                const unsigned char* refptr =
                    (const unsigned char*)std::memchr(ptr+a, data[0], cap-a);
                if(!refptr) break;
                a = refptr - ptr;
                unsigned compare_size = std::min((long)data.size(), (long)(append.OldSize - a));
                
                /* compare 1 byte less because find() already confirmed the first byte */
                if(std::memcmp(refptr+1, &data[1], compare_size-1) == 0)
                {
#if DEBUG_OVERLAP
                    std::printf("\nOVERLAP: ORIG=%u, NEW=%u, POS=%u, COMPARED %u\n",
                        (unsigned)cap, (unsigned)data.size(),
                        a, compare_size);
                    for(unsigned b=0; b<4+compare_size; ++b)
                        std::printf("%02X ", ptr[cap - compare_size+b-4]);
                    std::printf("\n");
                    for(unsigned b=0; b<4; ++b) std::printf("   ");
                    for(unsigned b=0; b<4+compare_size; ++b)
                        std::printf("%02X ", data[b]);
                    std::printf("\n");
#endif
                    result = a; /* Put it here. */
                    break;
                }
            }
            
            append.SetAppendPos(result, data.size());
        }
        return append;
    }

    void put_appended_raw(const AppendInfo& append, const std::vector<unsigned char>& data)
    {
        Unmap();

        const uint32_t cap = append.AppendBaseOffset + data.size();
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
            if(!data.empty()) write(fd, &data[0], data.size());
            
            filesize = cap;
            
            RemapFd(fd);
            
            close(fd);
            
            return;
        }
        
        /* not truncating */
        const uint32_t added_length = append.AppendedSize - append.OldSize;
        
        int fd = open(getfn().c_str(), O_RDWR | O_CREAT | O_LARGEFILE, 0644);
        if(fd < 0) { std::perror(getfn().c_str()); return; }
        pwrite(fd, &data[data.size() - added_length], added_length, append.OldSize);
        ftruncate(fd, filesize = append.AppendedSize);
        RemapFd(fd);
        close(fd);
    }
    
private:
    void get(std::vector<unsigned char>* raw,
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

public:
    void Unmap()
    {
        if(mmapped) { munmap(mmapped, filesize); mmapped = NULL; }
    }
    void Remap()
    {
        Unmap();
        if(is_compressed) return;
        
        int fd = open(getfn().c_str(), O_RDONLY | O_LARGEFILE);
        if(fd >= 0)
        {
            RemapFd(fd);
            close(fd);
        }
    }
private:
    void RemapFd(int fd)
    {
        void* p = mmap(NULL, filesize, PROT_READ|PROT_WRITE, MAP_SHARED, fd, 0);
        if(p != (void*)-1) mmapped = (unsigned char*)p;
    }
};
