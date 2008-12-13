#define DEBUG_APPEND  0
#define DEBUG_OVERLAP 0
#define DEBUG_FBLOCKINDEX 0

#include "../cromfs-defs.hh"

#include "mmapping.hh"
#include "boyermooreneedle.hh"
#include "datareadbuf.hh"
#include "autoclosefd.hh"
#include "append.hh"

#include <vector>
#include <string>
#include <cstdio>
#include <cstdlib>
#include <stdint.h>
#include <unistd.h>
#include <fcntl.h>

namespace fblock_private
{
    class fblock_storage
    {
    private:
        int fblock_disk_id;
        bool is_compressed;
        uint_fast32_t filesize;
        time_t last_access;
        MemMappingType<false> mmapped;
    public:
        fblock_storage()
            : fblock_disk_id(),
              is_compressed(false),
              filesize(0),
              last_access(0),
              mmapped()
        {
            static int disk_id = 0;
            fblock_disk_id = disk_id++;
            mmapped.Unmap();
        }
        fblock_storage(int disk_id)
            : fblock_disk_id(disk_id),
              is_compressed(false),
              filesize(0),
              last_access(0),
              mmapped()
        {
            mmapped.Unmap();
            
            Check_Existing_File();
        }
        
        void Check_Existing_File();
        
        bool is_uncompressed() const { return !is_compressed; }
        bool is_mmapped()      const { return mmapped; }
        uint_fast64_t getfilesize() const { return filesize; }
        
        const std::string getfn() const;
        
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
        
        void put_raw(const std::vector<unsigned char>& raw);
        
        void put_compressed(const std::vector<unsigned char>& compressed);

        void put(const std::vector<unsigned char>& /*raw*/,
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
        
    private:
        void get(std::vector<unsigned char>* raw,
                 std::vector<unsigned char>* compressed);

    public:
        void InitDataReadBuffer(DataReadBuffer& Buffer, uint_fast32_t& size);
        void InitDataReadBuffer(DataReadBuffer& Buffer, uint_fast32_t& size,
                                uint_fast32_t req_offset,
                                uint_fast32_t req_size);

        void EnsureMMapped()
        {
            if(is_compressed) put_raw(get_raw());
            if(!mmapped) Remap();
        }

        void Unmap() { mmapped.Unmap(); }
        void Remap()
        {
            Unmap();
            if(is_compressed) return;

            const autoclosefd fd(open(getfn().c_str(), O_RDWR | O_LARGEFILE));
            if(fd >= 0) RemapFd(fd);
            // fd will be automcally closed.
        }
        uint_fast32_t size()
        {
            if(is_compressed) return get_raw().size();
            return filesize;
        }
        
        time_t get_last_access() const { return last_access; }
        
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
}

class mkcromfs_fblock
{
private:
    mutable fblock_private::fblock_storage storage;

public:
    mkcromfs_fblock()            : storage()        { }
    mkcromfs_fblock(int disk_id) : storage(disk_id) { }

    void EnsureMMapped() const { storage.EnsureMMapped(); }

    bool is_uncompressed() const { return storage.is_uncompressed(); }
    uint_fast64_t getfilesize() const { return storage.getfilesize(); }

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

    void InitDataReadBuffer(DataReadBuffer& Buffer, uint_fast32_t& size) const
        { storage.InitDataReadBuffer(Buffer,size); }
    void InitDataReadBuffer(DataReadBuffer& Buffer, uint_fast32_t& size,
                            uint_fast32_t req_offset,
                            uint_fast32_t req_size) const
        { storage.InitDataReadBuffer(Buffer,size,req_offset,req_size); }

    time_t get_last_access() const { return storage.get_last_access(); }

    typedef fblock_private::fblock_storage::undo_t undo_t;
    undo_t create_backup() const { return storage.create_backup(); }
    void restore_backup(undo_t b) { storage.restore_backup(b); }
    
    size_t size() const { return storage.size(); }
};

/* This is the actual front end for fblocks in mkcromfs.
 * However, references to mkcromfs_fblock are allowed.
 */
class mkcromfs_fblockset
{
public:
    mkcromfs_fblockset(): index(), fblocks() { }
    ~mkcromfs_fblockset();
    
    inline size_t size() const { return fblocks.size(); }

    const mkcromfs_fblock& operator[] (size_t index) const
        { return fblocks[index]; }
    
    mkcromfs_fblock& operator[] (size_t index);
    
public:
    int FindFblockThatHasAtleastNbytesSpace(size_t howmuch) const
        { return index.FindAtleastNbytesSpace(howmuch); }
    void UpdateFreeSpaceIndex(cromfs_fblocknum_t fnum, size_t howmuch)
        { index.Update(fnum, howmuch); }

private:
    struct index_type
    {
        typedef std::multimap<size_t/*room*/, cromfs_fblocknum_t> room_index_t;
        room_index_t room_index;
        typedef std::map<cromfs_fblocknum_t, room_index_t::iterator> block_index_t;
        block_index_t block_index;

        int FindAtleastNbytesSpace(size_t howmuch) const;
        void Update(cromfs_fblocknum_t index, size_t howmuch);

        index_type(): room_index(), block_index() { }
    } index;

public:
    struct undo_t
    {
        size_t n_fblocks;
        std::vector<mkcromfs_fblock::undo_t> fblock_state;
        index_type fblock_index;

        undo_t() : n_fblocks(),fblock_state(),fblock_index() { } // -Weffc++
    };
    
    undo_t create_backup() const;
    void restore_backup(const undo_t& e);
    
    void FreeSomeResources();

private:
    std::vector<mkcromfs_fblock> fblocks;
};

extern void set_fblock_name_pattern(const std::string& pat);
