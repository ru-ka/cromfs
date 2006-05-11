#define _LARGEFILE64_SOURCE

#include <vector>
#include <cstdio>
#include <cstdlib>
#include <map>
#include <algorithm>
#include <sstream>

#include <unistd.h>
#include <dirent.h>
#include <fcntl.h>
#include <getopt.h>
#include <errno.h>
#include <sys/time.h>

#include "lzma.hh"

#include "../cromfs-defs.hh"

#include "datasource.hh"

#include "md5.hh"

#define DEFAULT_FSIZE 1048576
#define DEFAULT_BSIZE 65536

static bool DecompressWhenLookup = false;
static unsigned RandomCompressPeriod = 20;
static uint_fast32_t MinimumFreeSpace = 31;
static bool TryHardToReachFSIZE = false;

class mkcromfs_fblock
{
private:
    int fblock_disk_id;
    bool is_compressed;
public:
    mkcromfs_fblock()
    {
        static int disk_id = 0;
        fblock_disk_id = disk_id++;
        
        is_compressed = true;
    }
    
    bool is_uncompressed() const { return !is_compressed; }

    const std::string getfn() const
    {
        static int pid = getpid();
        char Buf[4096];
        std::sprintf(Buf, "/tmp/fblock_%d-%d", pid, fblock_disk_id);
        return Buf;
    }
    
    void Delete()
    {
        ::unlink(getfn().c_str());
    }
    
    void get(std::vector<unsigned char>& raw,
             std::vector<unsigned char>& compressed) const
    {
        get(&raw, &compressed);
    }

    const std::vector<unsigned char> get_raw() const
    {
        std::vector<unsigned char> raw;
        get(&raw, NULL);
        return raw;
    }

    const std::vector<unsigned char> get_compressed() const
    {
        std::vector<unsigned char> compressed;
        get(NULL, &compressed);
        return compressed;
    }
    
    void put_raw(const std::vector<unsigned char>& raw)
    {
        is_compressed = false;
        FILE* fp = std::fopen(getfn().c_str(), "wb");
        size_t res = std::fwrite(&raw[0], 1, raw.size(), fp);
        std::fclose(fp);
        if(res != raw.size())
        {
            // Possibly, out of disk space? Try to save compressed instead.
            put_compressed(LZMACompress(raw));
        }
    }
    void put_compressed(const std::vector<unsigned char>& compressed)
    {
        is_compressed = true;
        FILE* fp = std::fopen(getfn().c_str(), "wb");
        std::fwrite(&compressed[0], 1, compressed.size(), fp);
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

private:
    void get(std::vector<unsigned char>* raw,
             std::vector<unsigned char>* compressed) const
    {
        FILE* fp = std::fopen(getfn().c_str(), "rb");
        if(!fp)
        {
            static const std::vector<unsigned char> dummy;
            if(raw)        *raw = dummy;
            if(compressed) *compressed = dummy;
            return;
        }
        
        std::fseek(fp, 0, SEEK_END);
        
        std::vector<unsigned char> result( std::ftell(fp) );
        
        std::fseek(fp, 0, SEEK_SET);
        
        std::fread(&result[0], 1, result.size(), fp);
        std::fclose(fp);
        
        if(is_compressed)
        {
            if(compressed) *compressed = result;
            if(raw)        *raw = LZMADeCompress(result);
        }
        else
        {
            if(compressed) *compressed = LZMACompress(result);
            if(raw)        *raw = result;
        }
    }
};

const std::string ReportSize(uint_fast64_t size)
{
    std::stringstream st;
    st.flags(std::ios_base::fixed);
    st.precision(2);
    if(size < 90000llu) st << size << " bytes";
    else if(size < 1500000llu)    st << (size/1e3) << " kB";
    else if(size < 1500000000llu) st << (size/1e6) << " MB";
    else if(size < 1500000000000llu) st << (size/1e9) << " GB";
    else if(size < 1500000000000000llu) st << (size/1e12) << " TB";
    else st << (size/1e15) << " PB";
    return st.str();
}

class cromfs
{
public:
    cromfs(unsigned fsize, unsigned bsize)
    {
        FSIZE = fsize;
        BSIZE = bsize;
        bytes_of_files = 0;
    }
    ~cromfs()
    {
        for(unsigned a=0; a<fblocks.size(); ++a)
        {
            fblocks[a].Delete();
        }
    }
    
    void WriteTo(int fd)
    {
        std::vector<unsigned char> raw_root_inode   = encode_inode(rootdir);
        cromfs_inode_internal inotab_inode;
        { datasource_vector inotab_source(inotab);
          inotab_inode.mode = 0x12345678;
          inotab_inode.time = time(NULL);
          inotab_inode.links = 1;
          inotab_inode.bytesize = inotab.size();
          inotab_inode.blocklist = Blockify(inotab_source); }
        std::vector<unsigned char> raw_inotab_inode = encode_inode(inotab_inode);
        
        fprintf(stderr, "Compressing %u block records (%u bytes each)...",
            blocks.size(), sizeof(blocks[0])); fflush(stderr);
        std::vector<unsigned char> raw_blktab
            ((unsigned char*)&*blocks.begin(),
             (unsigned char*)&*blocks.end() /* Not really standard here */
            );
        raw_blktab = LZMACompress(raw_blktab);
        fprintf(stderr, " compressed into %s\n", ReportSize(raw_blktab.size()).c_str());
        
        unsigned char Superblock[0x38];
        uint_fast64_t root_ino_addr   = sizeof(Superblock);
        uint_fast64_t inotab_ino_addr = root_ino_addr + raw_root_inode.size();
        uint_fast64_t blktab_addr = inotab_ino_addr + raw_inotab_inode.size();
        uint_fast64_t fblktab_addr = blktab_addr + raw_blktab.size();

        W64(Superblock+0x00, CROMFS_SIGNATURE);
        W64(Superblock+0x08, blktab_addr);
        W64(Superblock+0x10, fblktab_addr);
        W64(Superblock+0x18, inotab_ino_addr);
        W64(Superblock+0x20, root_ino_addr);
        W32(Superblock+0x28, FSIZE);
        W32(Superblock+0x2C, BSIZE);
        W64(Superblock+0x30, bytes_of_files);
        
        lseek64(fd, 0, SEEK_SET);
        
        write(fd, Superblock, sizeof(Superblock));
        write(fd, &raw_root_inode[0],   raw_root_inode.size());
        write(fd, &raw_inotab_inode[0], raw_inotab_inode.size());

        lseek64(fd, blktab_addr, SEEK_SET);
        write(fd, &raw_blktab[0],       raw_blktab.size());
        
        uint_fast64_t compressed_total = 0;
        uint_fast64_t uncompressed_total = 0;
        
        lseek64(fd, fblktab_addr, SEEK_SET);
            
        for(unsigned a=0; a<fblocks.size(); ++a)
        {
            fprintf(stderr, "\rWriting fblock %u...", a); fflush(stderr);
            
            char Buf[64];
            std::vector<unsigned char> fblock, fblock_raw;
            fblocks[a].get(fblock_raw, fblock);
            
            fprintf(stderr, " %u bytes       ", fblock.size());
            
            W64(Buf, fblock.size());
            write(fd, Buf, 4);
            write(fd, &fblock[0], fblock.size());
            
            compressed_total   += FSIZE;
            uncompressed_total += fblock_raw.size();
        }
        fprintf(stderr,
            "\n%u fblocks were written: %s = %.2f %% of %s\n",
            fblocks.size(),
            ReportSize(compressed_total).c_str(),
            compressed_total * 100.0 / (double)uncompressed_total,
            ReportSize(uncompressed_total).c_str()
           );
        uint_fast64_t file_size = lseek64(fd, 0, SEEK_CUR);
        fprintf(stderr,
            "Filesystem size: %s = %.2f %% of original %s\n",
            ReportSize(file_size).c_str(),
            file_size * 100.0 / (double)bytes_of_files,
            ReportSize(bytes_of_files).c_str()
               );
    }
    
    void WalkRootDir(const std::string& path)
    {
        cromfs_inode_internal inode;
        
        cromfs_dirinfo dirinfo = WalkDir(path);
        
        std::vector<unsigned char> Buf = encode_directory(dirinfo);
        datasource_vector f(Buf);
        
        inode.mode     = S_IFDIR | 0777;
        inode.time     = time(NULL);
        inode.links    = dirinfo.size();
        inode.bytesize = f.size();
        inode.blocklist = Blockify(f);
        
        rootdir = inode;
    }
private:
    typedef std::pair<dev_t,ino_t> hardlinkdata;
    typedef std::map<hardlinkdata, cromfs_inodenum_t> hardlinkmap_t;
    hardlinkmap_t hardlink_map;
    
    void try_find_hardlink_file(dev_t dev, ino_t ino)
    {
        hardlinkdata d(dev, ino);
        hardlinkmap_t::const_iterator i = hardlink_map.find(d);
        if(i != hardlink_map.end())
        {
            throw i->second;
        }
    }

    cromfs_dirinfo WalkDir(const std::string& path)
    {
        cromfs_dirinfo dirinfo;
        
        DIR* dir = opendir(path.c_str());
        if(!dir) { std::perror(path.c_str()); return dirinfo; }
        
        std::vector<std::string> entries;
        while(dirent* ent = readdir(dir))
        {
            const std::string entname = ent->d_name;
            if(entname == "." || entname == "..") continue;
            entries.push_back(entname);
        }
        
        std::sort(entries.begin(), entries.end());
        
        for(unsigned a=0; a<entries.size(); ++a)
        {
            const std::string& entname = entries[a];
            struct stat st;
            const std::string pathname = path + "/" + entname;
            
            printf("%s ...\n", pathname.c_str());
            
            if(lstat(pathname.c_str(), &st) < 0)
            {
                std::perror(pathname.c_str());
                continue;
            }
            
            try
            {
                try_find_hardlink_file(st.st_dev, st.st_ino);

                cromfs_inode_internal inode;
                inode.mode     = st.st_mode;
                inode.time     = st.st_mtime;
                inode.links    = 1; //st.st_nlink;
                inode.bytesize = 0;
                inode.rdev     = 0;
                
                if(S_ISDIR(st.st_mode))
                {
                    cromfs_dirinfo dirinfo = WalkDir(pathname);
                    std::vector<unsigned char> Buf = encode_directory(dirinfo);
                    datasource_vector f(Buf);
                    inode.links     = dirinfo.size();
                    inode.bytesize  = f.size();
                    inode.blocklist = Blockify(f);

                    bytes_of_files += inode.bytesize;
                }
                else if(S_ISLNK(st.st_mode))
                {
                    std::vector<unsigned char> Buf(4096);
                    int res = readlink(pathname.c_str(), (char*)&Buf[0], Buf.size());
                    if(res < 0) { std::perror(pathname.c_str()); continue; }
                    Buf.resize(res);
                    
                    datasource_vector f(Buf);
                    inode.bytesize = f.size();
                    inode.blocklist = Blockify(f);

                    bytes_of_files += inode.bytesize;
                }
                else if(S_ISREG(st.st_mode))
                {
                    int fd = open(pathname.c_str(), O_RDONLY | O_LARGEFILE);
                    if(fd < 0) { std::perror(pathname.c_str()); continue; }
                    
                    datasource_file f(fd);
                    inode.bytesize  = f.size();
                    inode.blocklist = Blockify(f);
                    
                    bytes_of_files += inode.bytesize;
                    
                    close(fd);
                }
                else if(S_ISCHR(st.st_mode) || S_ISBLK(st.st_mode))
                {
                    inode.rdev      = st.st_rdev;
                }
                
                cromfs_inodenum_t inonum = find_or_create_inode(inode);
                
                dirinfo[entname] = inonum;
                
                hardlink_map[hardlinkdata(st.st_dev, st.st_ino)] = inonum;
            }
            catch(cromfs_inodenum_t inonum)
            {
                /* A hardlink was found! */
                
                printf("- reusing inode %ld (hardlink)\n", (long)inonum);
                
                /* Reuse the same inode number. */
                dirinfo[entname] = inonum;
                
                cromfs_inode_internal inode = load_inode(inonum);
                ++inode.links;
                resave_inode(inonum, inode, true);
            }
        }
        
        closedir(dir);
        
        EnsureAllAreCompressed();
        
        return dirinfo;
    }

    const std::vector<cromfs_blocknum_t> Blockify(datasource_t& data)
    {
        std::vector<cromfs_blocknum_t> blocklist;
        uint_fast64_t nbytes = data.size();
        while(nbytes > 0)
        {
            uint_fast64_t eat = nbytes;
            if(eat > BSIZE) eat = BSIZE;
            
            std::printf(" - %llu/%llu... ", eat, nbytes);
            std::fflush(stdout);
            
            std::vector<unsigned char> buf = data.read(eat);
            blocklist.push_back(find_or_create_block(buf));
            
            nbytes -= eat;
        }
        
        return blocklist;
    }
    
    const std::vector<unsigned char> encode_inode(const cromfs_inode_internal& inode)
    {
        std::vector<unsigned char> result(0x18 + 4 + 4*inode.blocklist.size());
        put_inode(&result[0], inode);
        return result;
    }
    
    void put_inode(unsigned char* inodata, const cromfs_inode_internal& inode,
                   bool ignore_blocks = false)
    {
        W32(&inodata[0x00], inode.mode);
        W32(&inodata[0x04], inode.time);
        W32(&inodata[0x08], inode.links);
        W32(&inodata[0x0C], inode.rdev);
        W64(&inodata[0x10], inode.bytesize);
        
        if(ignore_blocks) return;
        
        for(unsigned a=0; a<inode.blocklist.size(); ++a)
            W32(&inodata[0x18+a*4], inode.blocklist[a]);
    }
    
    void resave_inode(cromfs_inodenum_t inonum, const cromfs_inode_internal& inode,
                      bool ignore_blocks)
    {
        if(inonum == 1) { rootdir = inode; return; }
        uint_fast64_t pos = (inonum-2)*4;
        unsigned char* inodata = &inotab[pos];
        put_inode(inodata, inode, ignore_blocks);
    }
    
    const cromfs_inode_internal load_inode(cromfs_inodenum_t inonum) const
    {
        if(inonum == 1) { return rootdir; }
        
        uint_fast64_t pos = (inonum-2)*4;
        const unsigned char* inodata = &inotab[pos];
        
        cromfs_inode_internal inode;
        
        inode.mode     = R32(&inodata[0x00]);
        inode.time     = R32(&inodata[0x04]);
        inode.links    = R32(&inodata[0x08]);
        inode.rdev     = R32(&inodata[0x0C]);
        inode.bytesize = R64(&inodata[0x10]);
        
        return inode;
    }
    
    const cromfs_inodenum_t
        find_or_create_inode(const cromfs_inode_internal& inode)
    {
        uint_fast64_t pos = inotab.size();
        unsigned addsize = 0x18 + 4 * inode.blocklist.size();
        inotab.resize(inotab.size() + addsize);

        cromfs_inodenum_t result = (pos/4)+2;
        
        resave_inode(result, inode, false);
        
        return result;
    }
    
    const std::vector<unsigned char>
        encode_directory(const cromfs_dirinfo& dir) const
    {
        std::vector<unsigned char> result(4 + 4*dir.size());
        std::vector<unsigned char> entries;
        entries.reserve(dir.size() * (8 + 10)); // 10 = guestimate of average fn length
        
        W32(&result[0], dir.size());
        
        unsigned entrytableoffs = result.size();
        unsigned entryoffs = 0;
        
        unsigned diroffset=0;
        for(cromfs_dirinfo::const_iterator i = dir.begin(); i != dir.end(); ++i)
        {
            const std::string&     name = i->first;
            const cromfs_inodenum_t ino = i->second;
            
            W32(&result[4 + diroffset*4], entrytableoffs + entryoffs);
            
            entries.resize(entryoffs + 8 + name.size() + 1);
            
            W64(&entries[entryoffs], ino);
            std::memcpy(&entries[entryoffs+8], name.c_str(), name.size()+1);
            
            entryoffs = entries.size();
            ++diroffset;
        }
        result.insert(result.end(), entries.begin(), entries.end());
        return result;
    }
    
    const std::vector<unsigned char>
        encode_symlink(const std::string& target) const
    {
        std::vector<unsigned char> result(target.size());
        std::memcpy(&result[0], target.data(), target.size());
        return result;
    }
    
    const std::vector<unsigned char>& encode_inotab() const { return inotab; }
    
    const cromfs_blocknum_t
        find_or_create_block(const std::vector<unsigned char>& data)
    {
        // Find the fblock that contains this given data, or if that's
        // not possible, find out which fblock to append to, or whether
        // to create a new fblock.
        
        /* Use MD5 to find the identical block.
         * An option would be to use exhaustive search, to decompress each
         * and every fblock and see if they contain this data or at least
         * a part of it.
         */
        
        MD5c md5((const char*)&data[0], data.size());
        std::multimap<MD5c, cromfs_blocknum_t>::const_iterator i = block_index.find(md5);
        if(i != block_index.end())
        {
            for(;;)
            {
                cromfs_blocknum_t blocknum = i->second;
                const cromfs_block_storage& block = blocks[blocknum];
                
                if(block_is(block, data))
                {
                    printf(" reused block %u\n", (unsigned)blocknum);
                    return blocknum;
                }
                ++i;
                if(i == block_index.end()
                || i->first != md5) break;
            }
        }
        
        cromfs_block_storage block = create_new_block(data);
        cromfs_blocknum_t blockno = blocks.size();
        block_index.insert(std::make_pair(md5, blockno));
        blocks.push_back(block);
        return blockno;
    }
    
    bool block_is(const cromfs_block_storage& block,
                  const std::vector<unsigned char>& data)
    {
        mkcromfs_fblock& fblock = fblocks[block.fblocknum];
        std::vector<unsigned char> fblockdata = fblock.get_raw();
        
        if(DecompressWhenLookup && !fblock.is_uncompressed())
        {
            /* Try to speedup consequent lookups */
            /* This might hurt more than help, though */
            fblock.put_raw(fblockdata);
        }
        
        ssize_t size      = data.size();
        ssize_t remaining = fblockdata.size()-block.startoffs;
        if(remaining < size) return false;
        
        return std::memcmp(&fblockdata[block.startoffs], &data[0], size) == 0;
    }
    
    const cromfs_block_storage create_new_block(const std::vector<unsigned char>& data)
    {
        const std::vector<unsigned char> compressed_data = LZMACompress(data);
        
        /* Guess how many bytes of room this block needs in a fblock */
        uint_fast32_t guess_compressed_size = compressed_data.size();
        
        fblock_index_type::iterator i = fblock_index.lower_bound(guess_compressed_size);
        while(i != fblock_index.end())
        {
            const uint_fast32_t      old_room = i->first;
            const uint_fast32_t      old_compressed_size_guess = (FSIZE-4) - old_room;
            
            const cromfs_fblocknum_t fblocknum = i->second;
            mkcromfs_fblock& fblock = fblocks[fblocknum];
            
            std::vector<unsigned char> fblock_data_raw = fblock.get_raw();
            uint_fast32_t new_data_offset = AppendOrOverlapBlock(fblock_data_raw, data);

            /*
                TODO: If the appended result could not possibly become
                      too big, guess what the compressed size would
                      be (be very pessimistic!) and store it uncompressed.
            */

            const uint_fast32_t new_block_size_guess
                = old_compressed_size_guess + guess_compressed_size;
            
            uint_fast32_t new_block_size = new_block_size_guess;
            int_fast32_t new_remaining_room;
            new_remaining_room = (FSIZE-4) - new_block_size;
            
            if(new_remaining_room > (int_fast32_t)BSIZE
            || new_remaining_room > (int_fast32_t)data.size()*4)
            {
                /* Store uncompressed, use the estimated compressed size */
                fblock.put_raw(fblock_data_raw);

                printf(" in(%u), out(%u: ?->%u became %u->%u (GUESS), remain %u)\n",
                    guess_compressed_size,
                    (unsigned)fblocknum,
                    old_compressed_size_guess,
                    fblock_data_raw.size(), new_block_size,
                    new_remaining_room);
            }
            else
            {
                std::vector<unsigned char> fblock_new_compressed = LZMACompress(fblock_data_raw);
                new_block_size = fblock_new_compressed.size();

                new_remaining_room = (FSIZE-4) - new_block_size;
                
                printf(" in(%u), out(%u: ?->%u became %u->%u, remain %u)\n",
                    guess_compressed_size,
                    (unsigned)fblocknum,
                    old_compressed_size_guess,
                    fblock_data_raw.size(), new_block_size,
                    new_remaining_room);
                    
                if(new_remaining_room < 0)
                {
                    /* The fblock becomes too big, this is not acceptable */
                    /* Try to find a fblock that has more room */
                    ++i;
                    continue;
                }
                
                /* Accept this block */
                fblock.put(fblock_data_raw, fblock_new_compressed);
            }
            
            cromfs_block_storage result;
            result.fblocknum = fblocknum;
            result.startoffs = new_data_offset;
            fblock_index.erase(i);
            
            /* Minimum free space in the block */
            if((TryHardToReachFSIZE
               && (uint_fast32_t)new_remaining_room >= MinimumFreeSpace)
            || fblock.is_uncompressed())
            {
                i = fblock_index.insert(std::make_pair(new_remaining_room, result.fblocknum));
            }
            CompressOneRandomly();
            
            return result;
        }
        
        /* Create a new fblock */
        const std::vector<unsigned char>& fblock_new_compressed = compressed_data;
        int_fast32_t new_remaining_room = (FSIZE-4) - fblock_new_compressed.size();
        
        cromfs_block_storage result;
        result.fblocknum = fblocks.size();
        result.startoffs = 0;
        
        mkcromfs_fblock new_fblock;
        
        if(new_remaining_room >= (int_fast32_t)MinimumFreeSpace)
        {
            new_fblock.put(data, fblock_new_compressed);

            fblock_index.insert(std::make_pair(new_remaining_room, result.fblocknum));
        }
        else
        {
            /* Ensure it is compressed, because without an iterator,
             * the EnsureCompressed engine won't find it.
             */
            new_fblock.put_compressed(fblock_new_compressed);
        }
        
        printf(" in(%u), out(%u: new, remain %u)\n",
            fblock_new_compressed.size(),
            result.fblocknum,
            new_remaining_room);
        
        fblocks.push_back(new_fblock);

        CompressOneRandomly();
        
        return result;
    }

    uint_fast32_t AppendOrOverlapBlock(std::vector<unsigned char>& target,
                                       const std::vector<unsigned char>& data) const
    {
        if(data.empty()) return 0; /* A zerobyte block can be found anywhere. */
        
        uint_fast32_t result = target.size(); /* By default, insert at end. */
        for(unsigned a=0; ; ++a)
        {
            std::vector<unsigned char>::const_iterator
                apos = std::find(target.begin()+a, target.end(), data[0]);
            if(apos == target.end()) break;
            
            a = apos - target.begin();
            
            unsigned compare_size = std::min(data.size(), target.size() - a);
            
            /* compare 1 byte less because find() already confirmed the first byte */
            if(std::memcmp(&target[a+1], &data[1], compare_size-1) == 0)
            {
                result = a; /* Put it here. */
                break;
            }
        }
    
        /* Append all the remaining data to the end */
        unsigned common_context_length = std::min(data.size(), target.size() - result);
       // fprintf(stderr, "target=%u data=%u result=%u length=%u\n",
       //     target.size(), data.size(), result, common_context_length);
        target.insert(target.end(), data.begin()+common_context_length, data.end());
        return result;
    }
    
private:
    unsigned FSIZE, BSIZE;
    std::vector<cromfs_block_storage> blocks;
    std::multimap<MD5c, cromfs_blocknum_t> block_index;
    
    typedef std::multimap<uint_fast32_t, cromfs_fblocknum_t> fblock_index_type;
    std::vector<mkcromfs_fblock> fblocks;
    fblock_index_type fblock_index;

    std::vector<unsigned char> inotab;
    
    cromfs_inode_internal rootdir;
    
    uint_least64_t bytes_of_files;

private:
    void EnsureAllAreCompressed()
    {
    redo:
        for(fblock_index_type::iterator
            i = fblock_index.begin(); i != fblock_index.end(); ++i)
        {
            bool HadToCompress = EnsureCompressed(i);
            if(HadToCompress) goto redo;
        }
    }

    bool EnsureCompressed(fblock_index_type::iterator i)
    {
        cromfs_fblocknum_t fblocknum = i->second;

        /*
        fprintf(stderr, "Trying fblocknum %u / %u\n",
            fblocknum, fblocks.size());
        */
        
        mkcromfs_fblock& fblock = fblocks[fblocknum];
        if(fblock.is_uncompressed())
        {
            const std::vector<unsigned char> compressed = fblock.get_compressed();
            
            int_fast32_t new_remaining_room = (FSIZE - 4) - compressed.size();
            if(new_remaining_room < 0)
            {
                fprintf(stderr, "Integrity failure %d\n", new_remaining_room);
            }
            
            enum { keep,remove,reinsert} decision=keep;
            
            if((uint_fast32_t)new_remaining_room != i->first) decision=reinsert;
            if(new_remaining_room < (int_fast32_t)MinimumFreeSpace) decision=remove;
            
            if(decision >= remove)
                fblock_index.erase(i);
            if(decision >= reinsert)
                i = fblock_index.insert(std::make_pair(new_remaining_room, fblocknum));
            
            fblock.put_compressed(compressed);
            return true;
        }
        return false;
    }

    void CompressOneRandomly()
    {
        static unsigned counter = 0;
        if(!counter) counter = RandomCompressPeriod; else { --counter; return; }
    
        if(fblock_index.empty()) return;
        
        size_t count = std::rand() % fblock_index.size();
        
        fblock_index_type::iterator j = fblock_index.begin();
        std::advance(j, count);
        
        if(j != fblock_index.end()) EnsureCompressed(j);
    }

private:
    cromfs(cromfs&);
    void operator=(const cromfs&);
};

/*
static void TestCompression()
{
    std::vector<unsigned char> buf;
    for(unsigned a=0; a<40; ++a)
        buf.push_back("aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa"[a]);
    buf = LZMACompress(buf);
    fprintf(stderr, "buf size now = %u\n", buf.size());
    for(unsigned a=0; a<buf.size(); ++a) fprintf(stderr, " %02X", buf[a]);
    fprintf(stderr, "\n");

    buf = LZMADeCompress(buf);
    fprintf(stderr, "buf size now = %u\n", buf.size());

    for(unsigned a=0; a<buf.size(); ++a) fprintf(stderr, " %02X", buf[a]);
    fprintf(stderr, "\n");
}
*/

int main(int argc, char** argv)
{
    std::string path  = ".";
    std::string outfn = "cromfs.bin";
    
    long FSIZE = DEFAULT_FSIZE;
    long BSIZE = DEFAULT_BSIZE;

    for(;;)
    {
        int option_index = 0;
        static struct option long_options[] =
        {
            {"help",        0, 0,'h'},
            {"version",     0, 0,'V'},
            {"fsize",       1, 0,'f'},
            {"bsize",       1, 0,'b'},
            {"decompresslookups",
                            0, 0,'e'},
            {"randomcompressperiod",
                            1, 0,'r'},
            {"minfreespace",
                            1, 0,'s'},
            {"tryhard",     0, 0,'H'},
            {0,0,0,0}
        };
        int c = getopt_long(argc, argv, "hVf:b:er:s:H", long_options, &option_index);
        if(c==-1) break;
        switch(c)
        {
            case 'V':
            {
                printf("%s\n", VERSION);
                return 0;
            }
            case 'h':
            {
                printf(
                    "mkcromfs v"VERSION" - Copyright (C) 1992,2006 Bisqwit (http://iki.fi/bisqwit/)\n"
                    "\n"
                    "Usage: mkcromfs [<options>] <input_path> <target_image>\n"
                    " --help, -h         This help\n"
                    " --version, -V      Displays version information\n"
                    " --fsize, -f <size>\n"
                    "     Set the size of compressed data clusters. Default: 1048576\n"
                    "     Larger cluster size improves compression, but increases the memory\n"
                    "     usage during mount, and makes the filesystem a lot slower to generate.\n"
                    "     Should be set at least twice as large as bsize.\n"
                    " --bsize, -b <size>\n"
                    "     Set the size of file fragments. Default: 65536\n"
                    "     Smaller fragment size improves the merging of identical file content,\n"
                    "     but causes a larger block table to be generated, and slows down the\n"
                    "     creation of the filesystem.\n"
                    " --decompresslookups, -e\n"
                    "     Save decompressed data into a temporary file when it needs to be\n"
                    "     looked up for identical block verification. Speeds up mkcromfs\n"
                    "     somewhat, but may require as much free diskspace as the source\n"
                    "     files are together.\n"
                    " --randomcompressperiod, -r <value>\n"
                    "     Interval for randomly picking one fblock to compress. Default: 20\n"
                    "     The value has neglible effect on the compression ratio of the\n"
                    "     filesystem, but smaller values mean slower filesystem creation\n"
                    "     and bigger values mean more diskspace used by temporary files.\n"
                    " --minfreespace, -s <value>\n"
                    "     Minimum free space in a fblock to consider it a candidate. Default: 31\n"
                    "     Bigger values speed up the compression, but will cause some space\n"
                    "     being wasted that could have been used.\n"
                    " --tryhard, -H\n"
                    "     Try hard to reach full blocks of fsize bytes.\n"
                    "     Without this option, chances are that most of the fblocks will\n"
                    "     end up being about (fsize-bsize) in size. Which is usually not\n"
                    "     a problem if you increase fsize by the amount of bsize to\n"
                    "     compensate. :)\n"
                    "\n");
                return 0;
            }
            case 'f':
            {
                char* arg = optarg;
                long size = strtol(arg, &arg, 10);
                FSIZE = size;
                if(FSIZE < 64)
                {
                    fprintf(stderr, "mkcromfs: The minimum allowed fsize is 64. You gave %ld%s.\n", FSIZE, arg);
                    return -1;
                }
                if(FSIZE > 0x7FFFFFFF)
                {
                    fprintf(stderr, "mkcromfs: The maximum allowed fsize is 0x7FFFFFFF. You gave 0x%lX%s.\n", FSIZE, arg);
                    return -1;
                }
                break;
            }
            case 'b':
            {
                char* arg = optarg;
                long size = strtol(arg, &arg, 10);
                BSIZE = size;
                if(BSIZE < 8)
                {
                    fprintf(stderr, "mkcromfs: The minimum allowed bsize is 8. You gave %ld%s.\n", BSIZE, arg);
                    return -1;
                }
                break;
            }
            case 'e':
            {
                DecompressWhenLookup = true;
                break;
            }
            case 'r':
            {
                char* arg = optarg;
                long val = strtol(arg, &arg, 10);
                if(val < 1)
                {
                    fprintf(stderr, "mkcromfs: The minimum allowed randomcompressperiod is 1. You gave %ld%s.\n", val, arg);
                    return -1;
                }
                RandomCompressPeriod = val;
                break;
            }
            case 's':
            {
                char* arg = optarg;
                long val = strtol(arg, &arg, 10);
                if(val < 4)
                {
                    fprintf(stderr, "mkcromfs: The minimum allowed minfreespace is 4. You gave %ld%s.\n", val, arg);
                    return -1;
                }
                MinimumFreeSpace = val;
                break;
            }
            case 'H':
            {
                TryHardToReachFSIZE = true;
                break;
            }
        }
    }
    if(argc != optind+2)
    {
        fprintf(stderr, "mkcromfs: invalid parameters. See `mkcromfs --help'\n");
        return 1;
    }
    
    if(FSIZE < BSIZE)
    {
        fprintf(stderr,
            "mkcromfs: Warning: Your fsize %ld is smaller than your bsize %ld.\n"
            "  This is a bad idea, and causes problems especially\n"
            "  if your files aren't easy to compress.\n",
            (long)FSIZE, (long)BSIZE);
    }
    if((long)MinimumFreeSpace >= BSIZE)
    {
        fprintf(stderr,
            "mkcromfs: Warning: Your minfreespace %ld is quite high when compared\n"
            "  to your bsize %ld. It looks like a bad idea.\n",
            (long)MinimumFreeSpace, (long)BSIZE);
    }
    if((long)MinimumFreeSpace > FSIZE)
    {
        fprintf(stderr,
            "mkcromfs: Error: Your minfreespace %ld is larger than your fsize %ld.\n"
            "  It is not possible to create a cromfs volume under those constraints.\n",
            (long)MinimumFreeSpace, (long)FSIZE);
    }
    
    path  = argv[optind+0];
    outfn = argv[optind+1];
    
    int fd = open(outfn.c_str(), O_WRONLY | O_CREAT | O_LARGEFILE, 0644);
    if(fd < 0)
    {
        std::perror(outfn.c_str());
        return errno;
    }

    cromfs fs(FSIZE, BSIZE);
    fs.WalkRootDir(path.c_str());
    fprintf(stderr, "Writing %s...\n", outfn.c_str());
    
    ftruncate(fd, 0);
    fs.WriteTo(fd);
    close(fd);

    fprintf(stderr, "End\n");
    
    return 0;
}
