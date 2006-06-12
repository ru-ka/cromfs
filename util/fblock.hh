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

#include "boyermoore.hh"
#include "lzma.hh"

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
    
    int compare_raw_portion(const unsigned char* data,
                            uint_fast32_t data_size,
                            uint_fast32_t my_offs);
    
    int compare_raw_portion(const std::vector<unsigned char>& data, uint_fast32_t offs)
    {
        return compare_raw_portion(&data[0], data.size(), offs);
    }
    
#if 0
    int compare_raw_portion_another
       (mkcromfs_fblock& another,
        uint_fast32_t data_size,
        uint_fast32_t my_offs,
        uint_fast32_t other_offs)
    {
        if(is_compressed)
        {
            std::vector<unsigned char> raw = get_raw();
            if(DecompressWhenLookup) put_raw(raw);
            if(my_offs + data_size > raw.size()) return -1;
            return another.compare_raw_portion(&raw[my_offs], data_size, other_offs);
        }
        
        if(my_offs + data_size > filesize) return -1;
        
        if(!mmapped) Remap();
        if(mmapped)
        {
            return another.compare_raw_portion
                (mmapped + my_offs, data_size, other_offs);
        }
        
        /* mmap only works when the starting offset is aligned
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
                result = another.compare_raw_portion(pp+ignore, data_size, other_offs);
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
                    result = another.compare_raw_portion(&tmpbuf[0], data_size, other_offs);
            }
        }
        return result;
    }
#endif
    
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
    AppendInfo AnalyzeAppend(const BoyerMooreNeedle& needle,
                             uint_fast32_t minimum_pos = 0);

    void put_appended_raw(const AppendInfo& append, const std::vector<unsigned char>& data)
    {
        put_appended_raw(append, &data[0], data.size());
    }
    void put_appended_raw(const AppendInfo& append, const BoyerMooreNeedle& data)
    {
        put_appended_raw(append, &data[0], data.size());
    }
    
    void put_appended_raw(
        const AppendInfo& append,
        const unsigned char* data, const uint_fast32_t datasize);
    
private:
    void get(std::vector<unsigned char>* raw,
             std::vector<unsigned char>* compressed);

public:
    void Unmap()
    {
        if(mmapped) { munmap(mmapped, filesize); mmapped = NULL; }
    }
    void Remap()
    {
        Unmap();
        if(is_compressed) return;
        
        int fd = open(getfn().c_str(), O_RDWR | O_LARGEFILE);
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
