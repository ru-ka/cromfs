#include "cromfs-fblockfun.hh"

#include "../util/mkcromfs_sets.hh"

#include "boyermoore.hh"
#include "longfilewrite.hh"
#include "autoclosefd.hh"
#include "assert++.hh"
#include "lzma.hh"

#include <cstdlib>

using namespace fblock_private;

const char* GetTempDir()
{
    const char* t;
    t = std::getenv("TEMP"); if(t) return t;
    t = std::getenv("TMP"); if(t) return t;
    return "/tmp";
}

const std::string fblock_storage::getfn() const
{
    static const std::string tmpdir = GetTempDir();
    static const int pid = getpid();
    char Buf[4096];
    std::sprintf(Buf, "/fblock_%d-%d", pid, fblock_disk_id);
    //fprintf(stderr, "Buf='%s' tmpdir='%s'\n", Buf, tmpdir.c_str());
    return tmpdir + Buf;
}

void fblock_storage::put_raw(const std::vector<unsigned char>& raw)
{
    Unmap();
    
    is_compressed = false;
    
    size_t res=0;
    {autoclosefp fp = std::fopen(getfn().c_str(), "wb");
     res = std::fwrite(&raw[0], 1, filesize=raw.size(), fp);
    }
    
    if(res != raw.size())
    {
        std::fprintf(stderr, "fwrite: res=%d, should be %d\n", (int)res, (int)raw.size());
        // Possibly, out of disk space? Try to save compressed instead.
        put_compressed(LZMACompress(raw));
    }
    /* Remap(); */
}

void fblock_storage::put_compressed(const std::vector<unsigned char>& compressed)
{
    Unmap();
    
    //fprintf(stderr, "[1;mstoring compressed[m\n");
    is_compressed = true;
    
    autoclosefp fp = std::fopen(getfn().c_str(), "wb");
    std::fwrite(&compressed[0], 1, filesize=compressed.size(), fp);
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
    const autoclosefd fd = open(getfn().c_str(), O_RDONLY | O_LARGEFILE);

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
        autoclosefd fd = open(getfn().c_str(), O_RDWR | O_CREAT | O_LARGEFILE, 0644);
        if(fd < 0) { std::perror(getfn().c_str()); return; }

        is_compressed = false;
        
        assertvar(buf.size());
        assertvar(append.AppendBaseOffset);
        assert(buf.size() >= append.AppendBaseOffset);
        assertflush();
        
        // Write precending part:
        if(!buf.empty()) write(fd, &buf[0], append.AppendBaseOffset);
        // Write appended part:
        if(datasize > 0) write(fd, &data[0], datasize);
        
        /* Note: We need not worry about what comes after datasize.
         * If the block was fully submerged in the existing data,
         * we don't reach this far (already checked above).
         */
        
        filesize = cap;
        
        RemapFd(fd);
        
        // fd will be automatically closed.
        return;
    }
    
    /* not truncating */
    const uint32_t added_length = append.AppendedSize - append.OldSize;
    
    autoclosefd fd = open(getfn().c_str(), O_RDWR | O_CREAT | O_LARGEFILE, 0644);
    if(fd < 0) { std::perror(getfn().c_str()); return; }
    
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
        perror("pwrite");
    }
    ftruncate(fd, filesize = append.AppendedSize);
    RemapFd(fd);
    // fd will be automatically closed.
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
                if(!mmapped) Remap();
                if(mmapped) goto UseMMapping;
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
        
        if(!mmapped) Remap();
        if(mmapped)
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
            
            const autoclosefd fd = open(getfn().c_str(), O_RDWR | O_LARGEFILE);
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

void mkcromfs_fblockset::UnmapOneRandomlyButNot(cromfs_fblocknum_t forbid)
{
    //return;
    static cromfs_fblocknum_t counter = 0;
    if(counter < fblocks.size() && counter != forbid)
        fblocks[counter].Unmap();
    if(!fblocks.empty())
        counter = (counter+1) % fblocks.size();
}

void mkcromfs_fblockset::CompressOneRandomlyButNot(cromfs_fblocknum_t forbid)
{
    static unsigned counter = RandomCompressPeriod;
    if(!counter) counter = RandomCompressPeriod; else { --counter; return; }
    
    /* postpone it if there are no fblocks */
    if(fblocks.empty()) { counter=0; return; }
    
    const cromfs_fblocknum_t c = std::rand() % fblocks.size();
    
    /* postpone it if we hit the landmine */
    if(c == forbid) { counter=0; return; }
    
    mkcromfs_fblock& fblock = fblocks[c];

    /* postpone it if this fblock doesn't need compressing */
    if(!fblock.is_uncompressed()) { counter=0; return; }     
      
    if(fblock.is_uncompressed())
    {
        fblock.put_compressed(LZMACompress(fblock.get_raw()));
    }
}
