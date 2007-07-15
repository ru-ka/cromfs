#define DEBUG_APPEND  0
#define DEBUG_OVERLAP 0
#define DEBUG_FBLOCKINDEX 0

#include "mmapping.hh"
#include "boyermoore.hh"
#include "datareadbuf.hh"
#include "append.hh"
#include "lzma.hh"

#include <vector>
#include <string>
#include <cstdio>
#include <cstdlib>
#include <stdint.h>
#include <unistd.h>
#include <fcntl.h>

const char* GetTempDir();

class fblock_storage
{
private:
    int fblock_disk_id;
    bool is_compressed;
    uint_fast32_t filesize;
    MemMappingType<false> mmapped;
public:
    fblock_storage()
    {
        static int disk_id = 0;
        fblock_disk_id = disk_id++;
        filesize = 0;
        is_compressed = false;
        mmapped.Unmap();
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
         * It would be waste to discard the compression already done.
         */
        put_compressed(compressed);
        /*
        if(is_compressed)
            put_compressed(compressed);
        else
            put_raw(raw);
        */
    }
    
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
    
    int compare_raw_portion(const unsigned char* data,
                            uint_fast32_t data_size,
                            uint_fast32_t my_offs);
    
    int compare_raw_portion(const std::vector<unsigned char>& data, uint_fast32_t offs)
    {
        return compare_raw_portion(&data[0], data.size(), offs);
    }

    const AppendInfo AnalyzeAppend(
        const BoyerMooreNeedle& needle,
        uint_fast32_t minimum_pos = 0);

private:
    void get(std::vector<unsigned char>* raw,
             std::vector<unsigned char>* compressed);

    void InitDataReadBuffer(DataReadBuffer& Buffer, uint_fast32_t& size);
    void InitDataReadBuffer(DataReadBuffer& Buffer, uint_fast32_t& size,
                            uint_fast32_t req_offset,
                            uint_fast32_t req_size);

public:
    void Unmap() { mmapped.Unmap(); }
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
    uint_fast32_t size()
    {
        if(is_compressed) return get_raw().size();
        return filesize;
    }
    
private:
    void RemapFd(int fd) { mmapped.SetMap(fd, 0, filesize); }

public:
    typedef uint_fast32_t undo_t;
    undo_t create_backup() const
    {
        fblock_storage& t = const_cast<fblock_storage&> (*this);
        return t.size();
    }
    void restore_backup(undo_t b)
    {
        if(!is_compressed)
        {
            /* should be rather simple */
            Unmap();
            ::truncate(getfn().c_str(), filesize = b);
            return;
        }
        std::vector<unsigned char> data = get_raw();
        data.erase(data.begin() + b, data.end());
        put_raw(data);
    }
};

class mkcromfs_fblock
{
private:
    mutable fblock_storage storage;

public:
    /* Load file contents, analyze how to append/overlap but don't do it */
    const AppendInfo AnalyzeAppend(
        const BoyerMooreNeedle& needle,
        uint_fast32_t minimum_pos = 0) const
    {
        return storage.AnalyzeAppend(needle, minimum_pos);
    }

    int compare_raw_portion(const unsigned char* data,
                            uint_fast32_t data_size,
                            uint_fast32_t my_offs) const
    {
        return storage.compare_raw_portion(data, data_size, my_offs);
    }
    
    int compare_raw_portion(const std::vector<unsigned char>& data, uint_fast32_t offs) const
    {
        return storage.compare_raw_portion(data, offs);
    }
    
    bool is_uncompressed() const { return storage.is_uncompressed(); }
    void Unmap() { storage.Unmap(); }
    void Remap() { storage.Remap(); }
    void Delete() { storage.Delete(); }
    const std::vector<unsigned char> get_raw() const { return storage.get_raw(); }
    const std::vector<unsigned char> get_compressed() const { return storage.get_compressed(); }
    void put_raw(const std::vector<unsigned char>& raw)
        { storage.put_raw(raw); }
    void put_compressed(const std::vector<unsigned char>& compressed)
        { storage.put_compressed(compressed); }
    void put_appended_raw(const AppendInfo& append, const BoyerMooreNeedle& data)
        { storage.put_appended_raw(append, data); }
    void get(std::vector<unsigned char>& raw,
             std::vector<unsigned char>& compressed) const
        { storage.get(raw, compressed); }

    typedef fblock_storage::undo_t undo_t;
    undo_t create_backup() const { return storage.create_backup(); }
    void restore_backup(undo_t b) { storage.restore_backup(b); }
    
    uint_fast32_t size() const { return storage.size(); }
};
