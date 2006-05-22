#define _LARGEFILE64_SOURCE
#define __STDC_CONSTANT_MACROS

#include "../cromfs-defs.hh"

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

static bool DecompressWhenLookup = false;
static unsigned RandomCompressPeriod = 20;
static uint_fast32_t MinimumFreeSpace = 16;
static uint_fast32_t AutoIndexPeriod = 256;
static uint_fast32_t MaxFblockCountForBruteForce = 0;

static long FSIZE = 2097152;
static long BSIZE = 65536;

#include "lzma.hh"
#include "datasource.hh"
#include "fblock.hh"
#include "crc32.h"

#define NO_BLOCK ((cromfs_blocknum_t)~0ULL)
struct mkcromfs_block : public cromfs_block_storage
{
    cromfs_blocknum_t blocknum;
    
    mkcromfs_block() : blocknum(NO_BLOCK) { }
    
    void inherit(const cromfs_block_storage& b, cromfs_blocknum_t blockno)
    {
        cromfs_block_storage::operator=(b);
        blocknum = blockno;
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
private:
    uint_fast64_t amount_blockdata;
    uint_fast64_t amount_fblockdata;
public:
    cromfs()
    {
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
#if 0 /* for speed profiling */
        uint_fast64_t tmp=0;
        for(unsigned a=0; a<fblocks.size(); ++a)
            tmp += fblocks[a].get_raw().size();
        std::printf("fblock size total = %llu\n", tmp);
        return;
#endif

        std::vector<unsigned char> raw_root_inode   = encode_inode(rootdir);
        cromfs_inode_internal inotab_inode;

        std::printf("Blockifying the inode table...\n");
        { datasource_vector inotab_source(inotab);
          inotab_inode.mode = 0x12345678;
          inotab_inode.time = time(NULL);
          inotab_inode.links = 1;
          inotab_inode.bytesize = inotab.size();
          inotab_inode.blocklist = Blockify(inotab_source); }
        std::printf("Uncompressed inode table is %s (stored in fblocks, compressed).\n",
            ReportSize(inotab_inode.bytesize).c_str());
        
        std::vector<unsigned char> raw_inotab_inode = encode_inode(inotab_inode);
        
        std::printf("Compressing %u block records (%u bytes each)...",
            (unsigned)blocks.size(), (unsigned)sizeof(blocks[0])); fflush(stdout);
        std::vector<unsigned char> raw_blktab
            ((unsigned char*)&*blocks.begin(),
             (unsigned char*)&*blocks.end() /* Not really standard here */
            );
        raw_blktab = LZMACompress(raw_blktab);
        std::printf(" compressed into %s\n", ReportSize(raw_blktab.size()).c_str()); fflush(stdout);
        
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
            std::printf("\rWriting fblock %u...", a); std::fflush(stdout);
            
            char Buf[64];
            std::vector<unsigned char> fblock, fblock_raw;
            fblocks[a].get(fblock_raw, fblock);
            
            std::printf(" %u bytes       ", (unsigned)fblock.size());
            
            W64(Buf, fblock.size());
            write(fd, Buf, 4);
            write(fd, &fblock[0], fblock.size());
            
            std::fflush(stdout);
            
            compressed_total   += fblock.size();
            uncompressed_total += fblock_raw.size();
            
            /* Because this function can't be called twice (encode_inode does
             * changes to data that can't be repeated), might as well delete
             * temporary files while at it.
             */
            fblocks[a].Delete();
        }
        std::printf(
            "\n%u fblocks were written: %s = %.2f %% of %s\n",
            (unsigned)fblocks.size(),
            ReportSize(compressed_total).c_str(),
            compressed_total * 100.0 / (double)uncompressed_total,
            ReportSize(uncompressed_total).c_str()
           );
        uint_fast64_t file_size = lseek64(fd, 0, SEEK_CUR);
        std::printf(
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
        
        std::printf("Blockifying the root dir...\n");
        
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
            
            std::printf("%s ...\n", pathname.c_str());
            
            if(lstat(pathname.c_str(), &st) < 0)
            {
                std::perror(pathname.c_str());
                continue;
            }
            
            try
            {
                try_find_hardlink_file(st.st_dev, st.st_ino); /* throws if found */
                
                /* Not found, create new inode */
                cromfs_inode_internal inode;
                inode.mode     = st.st_mode;
                inode.time     = st.st_mtime;
                inode.links    = 1; //st.st_nlink;
                inode.bytesize = 0;
                inode.rdev     = 0;
                inode.uid      = st.st_uid;
                inode.gid      = st.st_gid;
                
                if(S_ISDIR(st.st_mode))
                {
                    cromfs_dirinfo dirinfo = WalkDir(pathname);
                    std::vector<unsigned char> Buf = encode_directory(dirinfo);
                    datasource_vector f(Buf);

                    std::printf("Blockifying %s ...\n", pathname.c_str());

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
                
                std::printf("- reusing inode %ld (hardlink)\n", (long)inonum);
                
                /* Reuse the same inode number. */
                dirinfo[entname] = inonum;
                
                cromfs_inode_internal inode = load_inode(inonum);
                ++inode.links;
                resave_inode(inonum, inode, true);
            }
        }
        
        closedir(dir);
        
        std::fflush(stdout);
        
        std::fflush(stdout);
        return dirinfo;
    }

    const std::vector<cromfs_blocknum_t> Blockify(datasource_t& data)
    {
        std::vector<cromfs_blocknum_t> blocklist;
        uint_fast64_t nbytes = data.size();
        
        uint_fast64_t consumption = 0;
        
        while(nbytes > 0)
        {
            uint_fast64_t eat = nbytes;
            if(eat > (uint_fast64_t)BSIZE) eat = BSIZE;
            
            std::printf(" - %u/%llu... ", (unsigned)eat, nbytes);
            std::fflush(stdout);
            
            amount_blockdata  = 0;
            amount_fblockdata = 0;
            
            std::vector<unsigned char> buf = data.read(eat);
            blocklist.push_back(find_or_create_block(buf));
            
            consumption += amount_blockdata;
            consumption += amount_fblockdata;
            consumption += 4; // size of entry in the inode
            
            nbytes -= eat;
        }
        
        const int_fast64_t minimum_optimal_overhead = 4 + sizeof(cromfs_block_storage);
        
        int_fast64_t overhead = consumption - data.size();
        if(overhead > 0)
            std::printf(" - overhead is %s%s\n",
                ReportSize(overhead).c_str(),
                overhead > minimum_optimal_overhead
                    ? ""
                    : ""
             );
        else
            std::printf(" - overhead is -%s%s\n",
                ReportSize(-overhead).c_str(),
                "; bytes saved");

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
        uint_fast32_t rdev_links = inode.links;
        if(S_ISCHR(inode.mode) || S_ISBLK(inode.mode)) rdev_links = inode.rdev;
    
        W32(&inodata[0x00], inode.mode);
        W32(&inodata[0x04], inode.time);
        W32(&inodata[0x08], rdev_links);
        W16(&inodata[0x0C], inode.uid);
        W16(&inodata[0x0E], inode.gid);
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
        
        uint_fast32_t rdev_links;
        inode.mode     = R32(&inodata[0x00]);
        inode.time     = R32(&inodata[0x04]);
        rdev_links     = R32(&inodata[0x08]);
        inode.uid      = R16(&inodata[0x0C]);
        inode.gid      = R16(&inodata[0x0E]);
        inode.bytesize = R64(&inodata[0x10]);
        
        if(S_ISCHR(inode.mode) || S_ISBLK(inode.mode))
            { inode.links = 1; inode.rdev = rdev_links; }
        else
            { inode.links = rdev_links; inode.rdev = 0; }

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
        
        /* Use CRC32 to find the identical block.
         *
         * An option would be to use exhaustive search, to decompress each
         * and every fblock and see if they contain this data or at least
         * a part of it. (Which is what MaxFblockCountForBruteForce now
         * does, to some extent.)
         */
        
        const crc32_t crc = crc32_calc(&data[0], data.size());
        block_index_t::iterator i = block_index.find(crc);
        if(i != block_index.end())
        {
            for(;;)
            {
                cromfs_blocknum_t& blocknum = i->second.blocknum;
                const cromfs_block_storage& block = i->second;
                
                if(!block_is(block, data))
                {
                    ++i;
                    if(i == block_index.end() || i->first != crc) break;
                    continue;
                }
                
                if(blocknum != NO_BLOCK)
                {
                    std::printf(" reused block %u [%u @ %u]\n",
                        (unsigned)blocknum,
                        (unsigned)block.fblocknum,
                        (unsigned)block.startoffs);
                    return blocknum;
                }
                
                blocknum = blocks.size();
                blocks.push_back(block);
                
                amount_blockdata += sizeof(block);

                std::printf(" reused indexed material [%u @ %u], became block %u\n",
                    (unsigned)block.fblocknum,
                    (unsigned)block.startoffs,
                    (unsigned)blocknum);
                return blocknum;
            }
        }
        
        cromfs_block_storage block = create_new_block(data);
        cromfs_blocknum_t blockno = blocks.size();
        mkcromfs_block b;
        b.inherit(block, blockno);
        block_index.insert(std::make_pair(crc, b));
        blocks.push_back(block);

        amount_blockdata += sizeof(block);

        return blockno;
    }
    
    bool block_is(const cromfs_block_storage& block,
                  const std::vector<unsigned char>& data) /* is_same_block */
    {
        return fblocks[block.fblocknum].compare_raw_portion(data, block.startoffs) == 0;
    }
    
    const cromfs_block_storage create_new_block(const std::vector<unsigned char>& data)
    {
        if(MaxFblockCountForBruteForce > 0 && fblocks.size() > 1)
        {
            std::vector<cromfs_fblocknum_t> candidates;
            candidates.reserve(fblocks.size());
            
            unsigned priority_candidates = 0;
            
            /* First candidate: The fblock that we would get without brute force */
            fblock_index_type::iterator i = fblock_index.lower_bound(data.size());
            if(i != fblock_index.end())
            {
                candidates.push_back(i->second);
                ++priority_candidates;
            }
            
            for(cromfs_fblocknum_t a=fblocks.size(); a-- > 0; )
            {
                /*__label__ skip_candidate; - not worth using gcc extension here */
                for(unsigned b=0; b<priority_candidates; ++b)
                    if(a == candidates[b]) goto skip_candidate;
                candidates.push_back(a);
              skip_candidate: ;
            }
            std::random_shuffle(candidates.begin()+priority_candidates, candidates.end());
            
            cromfs_fblocknum_t smallest = 0;
            uint_fast32_t smallest_size = 0;
            uint_fast32_t smallest_pos  = 0;
            int_fast32_t smallest_hole = 0;
            
            bool found_candidate = false;
            
            for(unsigned a = std::min((unsigned)candidates.size(),
                                      (unsigned)MaxFblockCountForBruteForce);
                a-- > 0; )
            {
                cromfs_fblocknum_t fblocknum = candidates[a];
                mkcromfs_fblock& fblock = fblocks[fblocknum];
                
                mkcromfs_fblock::AppendInfo appended = fblock.AnalyzeAppend(data);
                
                uint_fast32_t this_size = appended.AppendedSize - appended.OldSize;
                int_fast32_t hole_size = FSIZE - appended.AppendedSize;

                //printf("[cand %u:%u]", (unsigned)fblocknum, (unsigned)this_size);
                
                /* Don't do the smallest hole test. This would counter
                 * the purpose of MinimumFreeSpace.
                 */
                if(hole_size >= 0
                && (!found_candidate
                 || this_size < smallest_size
               /*  || (this_size == smallest_size && hole_size < smallest_hole) */
                  ))
                {
                    found_candidate = true;
                    smallest = fblocknum;
                    smallest_pos  = appended.AppendBaseOffset;
                    smallest_size = this_size;
                    smallest_hole = hole_size;
                    //printf("[!]");
                    if(smallest_size == 0) break; /* couldn't get better */
                }
            }
            
            /* Utilize the finding, if it's an overlap,
             * or it's an appension into a fblock that still has
             * so much room that it doesn't conflict with MinimumFreeSpace.
             * */
            if(found_candidate
               && (smallest_size < data.size()
                || ((smallest_pos+data.size()) < (FSIZE-MinimumFreeSpace))
                  )
              )
            {
                const cromfs_fblocknum_t fblocknum = smallest;
                //mkcromfs_fblock& fblock = fblocks[fblocknum];
                mkcromfs_fblock::AppendInfo appended;
                
                appended.SetAppendPos(smallest_pos, data.size());
                
                /* Find an iterator from fblock_index, if it exists. */
                fblock_index_type::iterator i = fblock_index.begin();
                while(i != fblock_index.end() && i->second != fblocknum) ++i;

#if DEBUG_FBLOCKINDEX
                if(i != fblock_index.end())
                    std::printf("[passing %d:%u]", (int)i->first, (unsigned)i->second);
#endif
                return AppendToFBlock(i, appended, fblocknum, data, false);
            }
        }
        
        fblock_index_type::iterator i = fblock_index.lower_bound(data.size());
        if(i == fblock_index.end())
        {
#if DEBUG_FBLOCKINDEX
            std::printf("[out of %u blocks in index, lower_bound(%u) gave end]\n",
                (unsigned)fblock_index.size(),
                (unsigned)data.size());
#endif
        }
        while(i != fblock_index.end())
        {
            try
            {
                return AppendToFBlock(i, i->second, data, true);
            }
            catch(bool)
            {
#if DEBUG_FBLOCKINDEX
                std::printf("[does not fit in block %u (%u)]\n- ", (unsigned)i->second, (unsigned)i->first);
#endif
                /* Try to find a fblock that has more room */
                ++i;
                continue;
             }
        }
        
#if DEBUG_FBLOCKINDEX
        std::printf("[new block]");
#endif
        
        /* Create a new fblock */
        cromfs_fblocknum_t fblocknum = fblocks.size();
        mkcromfs_fblock new_fblock;
        fblocks.push_back(new_fblock);
        
        amount_fblockdata += 4; /* size of FBLOCK without data */
        
        /* Note: The "false" in this parameter list tells not to throw exceptions. */
        return AppendToFBlock(fblock_index.end(), fblocknum, data, false);
    }

private:
    std::vector<cromfs_block_storage> blocks;

#ifdef USE_HASHMAP
    typedef __gnu_cxx::hash_multimap<crc32_t, mkcromfs_block> block_index_t;
#else
    typedef std::multimap<crc32_t, mkcromfs_block> block_index_t;
#endif
    block_index_t block_index;
    
    typedef std::multimap<int_fast32_t, cromfs_fblocknum_t> fblock_index_type;
    std::vector<mkcromfs_fblock> fblocks;
    fblock_index_type fblock_index;

    std::vector<unsigned char> inotab;
    
    cromfs_inode_internal rootdir;
    
    uint_least64_t bytes_of_files;

private:
    void EnsureCompressed(cromfs_fblocknum_t fblocknum)
    {
        mkcromfs_fblock& fblock = fblocks[fblocknum];
        if(fblock.is_uncompressed())
        {
            fblock.put_compressed(LZMACompress(fblock.get_raw()));
        }
    }
    
    void UnmapOneRandomlyButNot(cromfs_fblocknum_t forbid)
    {
        static cromfs_fblocknum_t counter = 0;
        if(counter < fblocks.size() && counter != forbid)
            fblocks[counter].Unmap();
        if(!fblocks.empty())
            counter = (counter+1) % fblocks.size();
    }

    void CompressOneRandomlyButNot(cromfs_fblocknum_t forbid)
    {
        static unsigned counter = 0;
        if(!counter) counter = RandomCompressPeriod; else { --counter; return; }
        
        /* postpone it if there are no fblocks */
        if(fblocks.empty()) { counter=0; return; }
        
        const cromfs_fblocknum_t c = std::rand() % fblocks.size();
        
        /* postpone it if we hit the landmine */
        if(c == forbid) { counter=0; return; }
        
        /* postpone it if this fblock doesn't need compressing */
        if(!fblocks[c].is_uncompressed()) { counter=0; return; }
        
        EnsureCompressed(c);
    }
    
    static const int CalcAutoIndexCount(int_fast32_t raw_size)
    {
        int_fast32_t a = (raw_size - BSIZE + AutoIndexPeriod);
        return a / (int_fast32_t)AutoIndexPeriod;
    }

    const cromfs_block_storage AppendToFBlock
        (fblock_index_type::iterator index_iterator,
         mkcromfs_fblock::AppendInfo& appended,
         const cromfs_fblocknum_t fblocknum,
         const std::vector<unsigned char>& data,
         bool prevent_overuse)
    {
        mkcromfs_fblock& fblock = fblocks[fblocknum];
        
        UnmapOneRandomlyButNot(fblocknum);
        CompressOneRandomlyButNot(fblocknum);
        
        const uint_fast32_t new_data_offset = appended.AppendBaseOffset;
        const uint_fast32_t new_raw_size = appended.AppendedSize;
        const uint_fast32_t old_raw_size = appended.OldSize;

        const int_fast32_t new_remaining_room = FSIZE - new_raw_size;
        
        if(new_remaining_room < 0)
        {
            /* The fblock becomes too big, this is not acceptable */
            if(prevent_overuse)
            {
                throw false;
            }
            std::printf(" (OVERUSE) ");
        }
        
        std::printf("block %u => [%u @ %u] size now %u, remain %d",
            (unsigned)blocks.size(),
            (unsigned)fblocknum,
            (unsigned)new_data_offset,
            (unsigned)new_raw_size,
            (int)new_remaining_room);
        
        if(new_data_offset < old_raw_size)
        {
            if(new_data_offset + data.size() < old_raw_size)
                std::printf(" (overlap fully)");
            else
                std::printf(" (overlap %d)", (int)(old_raw_size - new_data_offset));
        }

        fblock.put_appended_raw(appended, data);
        
        amount_fblockdata += new_raw_size - old_raw_size;
        
        /* Index all new checksum data */
        const int OldAutoIndexCount = std::max(CalcAutoIndexCount(old_raw_size),0);
        const int NewAutoIndexCount = std::max(CalcAutoIndexCount(new_raw_size),0);
        if(NewAutoIndexCount > OldAutoIndexCount && NewAutoIndexCount > 0)
        {
            std::vector<unsigned char> new_raw_data = fblock.get_raw();
            
            for(int count=OldAutoIndexCount+1; count<=NewAutoIndexCount; ++count)
            {
                uint_fast32_t startoffs = AutoIndexPeriod * (count-1);
                if(startoffs + BSIZE > new_raw_size) throw "error";
                /*
                std::printf("\nBlock reached 0x%X->0x%X bytes in size, (%d..%d), adding checksum for 0x%X; ",
                    old_raw_size, new_raw_size,
                    OldAutoMD5Count, NewAutoMD5Count,
                    startoffs);
                */
                const unsigned char* ptr = &new_raw_data[startoffs];
                const crc32_t crc = crc32_calc(ptr, BSIZE);
                
                /* Check if this checksum has already been indexed */
                block_index_t::iterator i = block_index.find(crc);
                if(i != block_index.end())
                {
                    /* Check if one of them matches this data, so that we don't
                     * add the same checksum data twice
                     */
                    std::vector<unsigned char> tmpdata(ptr, ptr+BSIZE);
                    for(;;)
                    {
                        if(block_is(i->second, tmpdata)) goto dont_add_crc;
                        ++i;
                        if(i == block_index.end() || i->first != crc) break;
                    }
                }
                
                { mkcromfs_block b;
                  b.fblocknum = fblocknum;
                  b.startoffs = startoffs;
                  block_index.insert(std::make_pair(crc, b));
                }
              dont_add_crc: ;
            }
        }
        
        std::printf("\n");
        
        cromfs_block_storage result;
        result.fblocknum = fblocknum;
        result.startoffs = new_data_offset;
        
        if(index_iterator != fblock_index.end())
        {
#if DEBUG_FBLOCKINDEX
            std::printf("[erasing %d:%u]\n", (int)index_iterator->first, (unsigned)index_iterator->second);
#endif
            fblock_index.erase(index_iterator);
        }
        
        /* If the block is uncompressed, preserve it fblock_index
         * so that CompressOneRandomly() may pick it some day.
         *
         * Otherwise, store it in the index only if it is still a candidate
         * for crunching more bytes into it.
         */
        
        if(new_remaining_room >= (int)MinimumFreeSpace)
        {
#if DEBUG_FBLOCKINDEX
            std::printf("[inserting %d:%u]\n", (int)new_remaining_room, (unsigned)result.fblocknum);
#endif
            index_iterator =
                fblock_index.insert(std::make_pair(new_remaining_room, result.fblocknum));
        }
        else
        {
#if DEBUG_FBLOCKINDEX
            std::printf("[not inserting %d:%u]\n", (int)new_remaining_room, (unsigned)result.fblocknum);
#endif
        }
        return result;
    }

    const cromfs_block_storage AppendToFBlock
        (fblock_index_type::iterator index_iterator,
         const cromfs_fblocknum_t fblocknum,
         const std::vector<unsigned char>& data,
         bool prevent_overuse)
    {
        mkcromfs_fblock& fblock = fblocks[fblocknum];

        mkcromfs_fblock::AppendInfo appended = fblock.AnalyzeAppend(data);
        return AppendToFBlock(index_iterator, appended, fblocknum, data, prevent_overuse);
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
    std::fprintf(stderr, "buf size now = %u\n", buf.size());
    for(unsigned a=0; a<buf.size(); ++a) std::fprintf(stderr, " %02X", buf[a]);
    std::fprintf(stderr, "\n");

    buf = LZMADeCompress(buf);
    std::fprintf(stderr, "buf size now = %u\n", buf.size());

    for(unsigned a=0; a<buf.size(); ++a) std::fprintf(stderr, " %02X", buf[a]);
    std::fprintf(stderr, "\n");
}
*/

int main(int argc, char** argv)
{
    std::string path  = ".";
    std::string outfn = "cromfs.bin";
    
    unsigned AutoIndexRatio = 16;
    
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
            {"autoindexratio",
                            1, 0,'a'},
            {"bruteforcelimit",
                            1, 0,'c'},
            {0,0,0,0}
        };
        int c = getopt_long(argc, argv, "hVf:b:er:s:a:c:", long_options, &option_index);
        if(c==-1) break;
        switch(c)
        {
            case 'V':
            {
                std::printf("%s\n", VERSION);
                return 0;
            }
            case 'h':
            {
                std::printf(
                    "mkcromfs v"VERSION" - Copyright (C) 1992,2006 Bisqwit (http://iki.fi/bisqwit/)\n"
                    "\n"
                    "Usage: mkcromfs [<options>] <input_path> <target_image>\n"
                    " --help, -h         This help\n"
                    " --version, -V      Displays version information\n"
                    " --fsize, -f <size>\n"
                    "     Set the size of compressed data clusters. Default: 2097152\n"
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
                    "     The value has no effect on the compression ratio of the filesystem,\n"
                    "     but smaller values mean slower filesystem creation and bigger values\n"
                    "     mean more diskspace used by temporary files.\n"
                    " --minfreespace, -s <value>\n"
                    "     Minimum free space in a fblock to consider it a candidate. Default: 16\n"
                    "     The value should be smaller than bsize, or otherwise it works against\n"
                    "     the fsize setting by making the fsize impossible to reach.\n"
                    "     Note: The bruteforcelimit algorithm ignores minfreespace.\n"
                    " --autoindexratio, -a <value>\n"
                    "     Defines the ratio of indexes to blocks which to use when creating\n"
                    "     the filesystem. Default value: 16\n"
                    "     For example, if your bsize is 10000 and autoindexratio is 10,\n"
                    "     it means that for each 10000-byte block, 10 index entries will\n"
                    "     be created. Larger values help compression, but will use more RAM.\n"
                    "     The RAM required is approximately:\n"
                    "         (total_filesize * autoindexratio * N / bsize) bytes\n"
                    "     where N is around 16 on 32-bit systems, around 28 on 64-bit\n"
                    "     systems, plus memory allocation overhead.\n"
                    " --bruteforcelimit, -c <value>\n"
                    "     Set the maximum number of randomly selected fblocks to search\n"
                    "     for overlapping content when deciding which fblock to append to.\n"
                    "     The default value, 0, means to do straight-forward selection\n"
                    "     based on the free space in the fblock.\n"
                    "     Note: If you use --bruteforcelimit, you should set minfreespace\n"
                    "     to a value larger than your bsize in order to leave some spare\n"
                    "     fblocks for the bruteforcing to test.\n"
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
                    std::fprintf(stderr, "mkcromfs: The minimum allowed fsize is 64. You gave %ld%s.\n", FSIZE, arg);
                    return -1;
                }
                if(FSIZE > 0x7FFFFFFF)
                {
                    std::fprintf(stderr, "mkcromfs: The maximum allowed fsize is 0x7FFFFFFF. You gave 0x%lX%s.\n", FSIZE, arg);
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
                    std::fprintf(stderr, "mkcromfs: The minimum allowed bsize is 8. You gave %ld%s.\n", BSIZE, arg);
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
                    std::fprintf(stderr, "mkcromfs: The minimum allowed randomcompressperiod is 1. You gave %ld%s.\n", val, arg);
                    return -1;
                }
                RandomCompressPeriod = val;
                break;
            }
            case 'a':
            {
                char* arg = optarg;
                long val = strtol(arg, &arg, 10);
                if(val < 1)
                {
                    std::fprintf(stderr, "mkcromfs: The minimum allowed autoindexratio is 1. You gave %ld%s.\n", val, arg);
                    return -1;
                }
                AutoIndexRatio = val;
                break;
            }
            case 's':
            {
                char* arg = optarg;
                long val = strtol(arg, &arg, 10);
                if(val < 1)
                {
                    std::fprintf(stderr, "mkcromfs: The minimum allowed minfreespace is 1. You gave %ld%s.\n", val, arg);
                    return -1;
                }
                MinimumFreeSpace = val;
                break;
            }
            case 'c':
            {
                char* arg = optarg;
                long val = strtol(arg, &arg, 10);
                if(val < 0)
                {
                    std::fprintf(stderr, "mkcromfs: The minimum allowed bruteforcelimit is 0. You gave %ld%s.\n", val, arg);
                    return -1;
                }
                MaxFblockCountForBruteForce = val;
                break;
            }
        }
    }
    if(argc != optind+2)
    {
        std::fprintf(stderr, "mkcromfs: invalid parameters. See `mkcromfs --help'\n");
        return 1;
    }
    
    if(FSIZE < BSIZE)
    {
        std::fprintf(stderr,
            "mkcromfs: Error: Your fsize %ld is smaller than your bsize %ld.\n"
            "  Cannot comply.\n",
            (long)FSIZE, (long)BSIZE);
    }
    if((long)MinimumFreeSpace >= BSIZE)
    {
        std::fprintf(stderr,
            "mkcromfs: Warning: Your minfreespace %ld is quite high when compared to your\n"
            "  bsize %ld. Unless you rely on bruteforcelimit, it looks like a bad idea.\n",
            (long)MinimumFreeSpace, (long)BSIZE);
    }
    if((long)MinimumFreeSpace >= FSIZE)
    {
        std::fprintf(stderr,
            "mkcromfs: Error: Your minfreespace %ld is larger than your fsize %ld.\n"
            "  Cannot comply.\n",
            (long)MinimumFreeSpace, (long)FSIZE);
    }
    
    AutoIndexPeriod = std::max((long)AutoIndexRatio, (long)(BSIZE / AutoIndexRatio));
    
    if(AutoIndexPeriod < 1)
    {
        std::fprintf(stderr,
            "mkcromfs: Error: Your autoindexratio %ld is larger than your bsize %ld.\n"
            "  Cannot comply.\n", (long)AutoIndexRatio, BSIZE);
    }
    if(AutoIndexPeriod <= 4)
    {
        char Buf[256];
        if(AutoIndexPeriod == 1) std::sprintf(Buf, "for every possible byte");
        else std::sprintf(Buf, "every %u bytes", (unsigned)AutoIndexPeriod);
        
        std::fprintf(stderr,
            "mkcromfs: The autoindexratio you gave, %ld, means that a _severe_ amount\n"
            "  of memory will be used by mkcromfs. An index will be added %s.\n"
            "  Just thought I should warn you.\n",
            (long)AutoIndexRatio, Buf);
    }
    
    path  = argv[optind+0];
    outfn = argv[optind+1];
    
    int fd = open(outfn.c_str(), O_WRONLY | O_CREAT | O_LARGEFILE, 0644);
    if(fd < 0)
    {
        std::perror(outfn.c_str());
        return errno;
    }

    cromfs fs;
    fs.WalkRootDir(path.c_str());
    std::fprintf(stderr, "Writing %s...\n", outfn.c_str());
    
    ftruncate(fd, 0);
    fs.WriteTo(fd);
    close(fd);

    std::fprintf(stderr, "End\n");
    
    return 0;
}
