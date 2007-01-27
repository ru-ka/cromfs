#define _LARGEFILE64_SOURCE
#define __STDC_CONSTANT_MACROS

#include "../cromfs.hh"
#include "util.hh"

#ifdef USE_HASHMAP
# include <ext/hash_set>
# include "../hash.hh"
#endif

#include <unistd.h>
#include <fcntl.h>
#include <stdio.h>
#include <errno.h>
#include <utime.h>

#include <getopt.h>

#include "fnmatch.hh"
#include "rangeset.hh"
#include "rangemultimap.hh"

#include <map>
#include <set>

static bool listing_mode  = false;
static bool simgraph_mode = false;
static bool use_sparse    = true;
static MatchingFileListType extract_files;
static MatchingFileListType exclude_files;

static bool MatchFile(const std::string& entname)
{
    return MatchFileFrom(entname, extract_files, true)
        && !MatchFileFrom(entname, exclude_files, false);
}

static const std::string DumpTime(uint_fast32_t time)
{
    time_t tmp = time;
    const struct tm* tm = localtime(&tmp);
    char Buf[512] = "";
    strftime(Buf, sizeof(Buf)-1, "%Y-%m%d-%H%M%S", tm);
    return Buf;
}

/* This object tries to merge several file writes
 * together to reduce the syscall traffic.
 */
class FileOutputter
{
public:
    explicit FileOutputter(int fild, uint_fast64_t esize)
        : fd(fild), bufpos(0), expected_size(esize)
    {
    }
    void write(const unsigned char* buf, uint_fast64_t size, uint_fast64_t offset)
    {
        const unsigned MaxBufSize = std::min(0x80000ULL, (unsigned long long)expected_size);

        //goto JustWriteIt;// DEBUG
        
        if(offset == 0 && size == expected_size) goto JustWriteIt;
        if(Buffer.capacity() != MaxBufSize) Buffer.reserve(MaxBufSize);

        if(offset != getbufend() || Buffer.size() + size > MaxBufSize)
        {
            FlushBuffer();
        }
        
        if(Buffer.empty()) bufpos = offset;
        
        if(offset == getbufend() && Buffer.size() + size <= MaxBufSize)
        {
            /* Append it to the buffer. */
            Buffer.insert(Buffer.end(), buf, buf+size);
        }
        else
        {
            /* If this data does not fit into the buffer, write it at once. */
        JustWriteIt: // arrived here also if we decided to skip buffering
            pwrite64(fd, buf, size, offset);
        }
    }
    ~FileOutputter()
    {
        FlushBuffer();
        close(fd);
    }
private:
    void FlushBuffer()
    {
        if(Buffer.empty()) return;
        pwrite64(fd, &Buffer[0], Buffer.size(), bufpos);
        Buffer.clear();
    }
    const uint_fast64_t getbufend() const { return bufpos + Buffer.size(); }
private:
    int fd;
    uint_fast64_t bufpos;
    uint_fast64_t expected_size;
    std::vector<unsigned char> Buffer;
private:
    /* no copies */
    void operator=(const FileOutputter&);
    FileOutputter(const FileOutputter&);
};

struct cromfs_block_index
{
private:
    uint_fast64_t value;
public:
    cromfs_block_index() : value(0) {}
    cromfs_block_index(uint_fast64_t v) : value(v) { }
    cromfs_block_index(const cromfs_block_storage& blk)
        : value(((uint_fast64_t)blk.fblocknum << 32) | blk.startoffs) { }

    cromfs_block_index
        operator+ (uint_fast64_t v) const { return value+v; }

    bool operator==(const cromfs_block_index& b) const { return value==b.value; }
    bool operator!=(const cromfs_block_index& b) const { return value!=b.value; }
    bool operator< (const cromfs_block_index& b) const { return value< b.value; }

    operator uint_fast64_t () const { return value; }
    
    cromfs_block_index
        operator- (cromfs_block_index v) const { return value - v; }
};

class cromfs_decoder: public cromfs
{
private:
    std::multimap<cromfs_fblocknum_t, cromfs_inodenum_t> fblock_users;
    std::multimap<cromfs_inodenum_t, std::string> inode_files;
    cromfs_dirinfo dir;

public:
    cromfs_decoder(int fd): cromfs(fd) { }
    
    void cleanup()
    {
        dir.clear();
        fblock_users.clear();
        inode_files.clear();
    }
    
    void do_simgraph()
    {
        cleanup();

        std::printf("<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n");
        std::printf("<simgraph>\n");
        std::printf(" <volume>\n");
        std::printf("  <total_size>%llu</total_size>\n", (unsigned long long)sblock.bytes_of_files);
        ScanDirectories();
        
        std::printf("  <num_inodes>%lu</num_inodes>\n", (unsigned long)inode_files.size());
        std::printf("  <num_files>%lu</num_files>\n", (unsigned long)dir.size());

        std::printf(" </volume>\n");
        std::printf(" <inodes>\n");
        
        std::vector<cromfs_inodenum_t> inodelist;
        
        cromfs_inodenum_t prev=0; bool first=true;
        for(std::multimap<cromfs_inodenum_t, std::string>::const_iterator
            i = inode_files.begin();
            i != inode_files.end();
            ++i)
        {
            if(first || prev != i->first)
            {
                if(!first)  std::printf("</inode>\n");
                first=false;
                prev = i->first;
                std::printf("  <inode id=\"%llu\">", (unsigned long long)prev);
                inodelist.push_back(prev);
            }
            std::printf("<file>%s</file>",
                FileNameToXML(i->second).c_str());
        }
        if(!first) std::printf("</inode>\n");
        std::printf(" </inodes>\n");
        
        std::printf(" <matches>\n");
        std::fflush(stdout);


#ifdef USE_HASHMAP
        typedef __gnu_cxx::hash_set<cromfs_inodenum_t> handled_inodes_t;
#else
        typedef std::set<cromfs_inodenum_t> handled_inodes_t;
#endif

        /* Create a map of which inodes cover what sections of the filesystem */
        rangemultimap<cromfs_block_index, cromfs_inodenum_t> range_map;
        for(unsigned a=0; a<inodelist.size(); ++a)
        {
            const cromfs_inodenum_t ino_a = inodelist[a];
            const rangeset<cromfs_block_index> range_a = create_rangeset(ino_a);
            for(rangeset<cromfs_block_index>::const_iterator
                i = range_a.begin(); i != range_a.end(); ++i)
            {
                range_map.set(i->lower, i->upper, ino_a);
            }
        }

        handled_inodes_t handled_inodes;

        for(unsigned a=0; a<inodelist.size(); ++a)
        {
            const cromfs_inodenum_t ino_a = inodelist[a];
            if(handled_inodes.find(ino_a) != handled_inodes.end()) continue;
            const uint_fast64_t size_a = read_inode(ino_a).bytesize;
            
            std::fprintf(stderr, "\r%u/%u", a,(unsigned)inodelist.size());
            
            if(!size_a)
            {
                /* Not a good idea to test zero-size files... */
                continue;
            }
            
            const rangeset<cromfs_block_index>& range_a = range_map.get_rangelist(ino_a);
            
            handled_inodes_t candidates;
            
            /* Get the list of all inodes that coincide
             * with the ranges occupied by this inode
             */
            for(rangeset<cromfs_block_index>::const_iterator
                i = range_a.begin(); i != range_a.end(); ++i)
            {
                fprintf(stderr, "s"); fflush(stderr);
                std::list<cromfs_inodenum_t> inolist =
                    range_map.get_valuelist(i->lower, i->upper);
                candidates.insert(inolist.begin(), inolist.end());
            }
            
            handled_inodes.insert(ino_a);

            handled_inodes_t handled_pairs;
            for(handled_inodes_t::const_iterator
                j = candidates.begin();
                j != candidates.end();
                ++j)
            {
                const cromfs_inodenum_t ino_b = *j;
                /* Ignore those that have already been tested */
                if(handled_inodes.find(ino_b) != handled_inodes.end()) continue;
                if(handled_pairs.find(ino_b)  != handled_pairs.end()) continue;
                handled_pairs.insert(ino_b);
                
                const uint_fast64_t size_b = read_inode(ino_b).bytesize;
                if(!size_b) { handled_inodes.insert(ino_b); continue; }
                
                const rangeset<cromfs_block_index>& range_b = range_map.get_rangelist(ino_b);
                rangeset<cromfs_block_index> intersect = range_a.intersect(range_b);
                if(intersect.empty()) continue;
                
                uint_fast64_t intersecting_size = 0;
                for(rangeset<cromfs_block_index>::const_iterator
                    i = intersect.begin();
                    i != intersect.end();
                    ++i)
                {
                    intersecting_size += i->upper - i->lower;
                }
                
                std::printf("  <match inode1=\"%llu\" inode2=\"%llu\">",
                    (unsigned long long)ino_a,
                    (unsigned long long)ino_b);

                /*
                std::printf("<!-- asize %llu, bsize %llu -->\n",
                    size_a, size_b);*/
                
                std::printf("<bytes>%llu</bytes><ratio>%.10f",
                    (unsigned long long)intersecting_size,
                    intersecting_size / (double)std::min(size_a, size_b)
                           );
                std::printf("</ratio></match>\n");
            }
            std::fflush(stdout);
        }
        
        std::printf(" </matches>\n");
        std::printf("</simgraph>\n");
    }
    
    void do_listing()
    {
        cleanup();
        ScanDirectories();
    }
    
    void do_extract(const std::string& targetdir)
    {
        cleanup();
        
        ScanDirectories();
        
        /*
         * This is written as one huge function, because there is a lot
         * of work to be done, and it is not straight-away obvious which
         * way is the right order to do each step. For example, hardlinks
         * could be created immediately after inodes, or after writing the
         * files, or after fixing up the symlinks. There are lots of data
         * that is shared between the steps (the file size counters, inode
         * number tables), that it's easiest to keep it as one whole function.
         * It allows keeping some liberty of reorganizing stuff around.
         *
         * Only the initial phase (directory scanning) is a clearly separate task.
         */
        
        std::printf("Creating %u inodes... (the files and directories)\n",
            (unsigned)dir.size()
               );

        uint_fast64_t expect_size = 0;
        unsigned expect_files = 0;
        unsigned expect_links = 0;
        
        std::map<cromfs_inodenum_t, std::string> seen_inodes;
        std::set<std::string> duplicate_inodes;
        
        umask(0);
        for(cromfs_dirinfo::iterator i = dir.begin(); i != dir.end(); ++i)
        {
            cromfs_inodenum_t inonum = i->second;
            
            cromfs_inode_internal ino = read_inode(inonum);
            std::string target = targetdir + "/" + i->first;
            
            if(seen_inodes.find(inonum) != seen_inodes.end())
            {
                /* Don't create the inode twice */
                duplicate_inodes.insert(i->first);
                continue;
            }
            seen_inodes[inonum] = i->first;
            
            int r = 0;
            if(S_ISDIR(ino.mode))
                r = mkdir( target.c_str(), ino.mode | 0700);
            else if(S_ISLNK(ino.mode))
            {
                // Create symlinks as regular files first.
                // This allows to construct them in pieces.
                // When symlink() is called, the entire name must be known.
                r = mknod( target.c_str(), S_IFREG | 0600, 0);
                if(r < 0 && errno == EEXIST)
                {
                    unlink( target.c_str() );
                    r = mknod( target.c_str(), S_IFREG | 0600, 0);
                }
            }
            else
            {
                r = mknod( target.c_str(), ino.mode | 0600, ino.rdev);
                if(r < 0 && errno == EEXIST)
                {
                    unlink( target.c_str() );
                    r = mknod( target.c_str(), S_IFREG | 0600, 0);
                }
            }
            
            if(S_ISREG(ino.mode)) ++expect_files;
            if(S_ISLNK(ino.mode)) ++expect_links;
            
            if(S_ISREG(ino.mode) || S_ISLNK(ino.mode))
            {
                // To help filesystem in optimizing the storage.
                // It does not matter if this call fails.
                truncate64( target.c_str(), ino.bytesize);
                expect_size += ino.bytesize;
            }
            
            if(r < 0)
            {
                perror(target.c_str());
            }

            /* Fix up permissions later. Now the important thing is to
             * be able to write into those files and directories, which
             * is why we used 0600 and 0700 here.
             */
            
            //printf("%s: %d\n", i->first.c_str(), (int) inonum);
        }
        
        uint_fast64_t total_written = 0;
        
        std::printf("Writing files... (%s in %u files and %u links)\n",
            ReportSize(expect_size).c_str(),
            expect_files,
            expect_links
                   );
        for(cromfs_fblocknum_t fblockno = 0; fblockno < fblktab.size(); ++fblockno)
        {
            /* Count the number of files/symlinks that require data
             * from this particular fblock.
             */
            unsigned nfiles = 0;

            for(std::multimap<cromfs_fblocknum_t, cromfs_inodenum_t>::iterator
                i = fblock_users.find(fblockno);
                i != fblock_users.end() && i->first == fblockno;
                ++i) ++nfiles;
            
            if(!nfiles)
            {
                /* This fblock only contains directories or parts of the
                 * inode table, or none of the selected files.
                 * Do not waste resources decompressing it now.
                 */
                std::printf("fblock %u / %u: skipping\n",
                    (unsigned)fblockno, (unsigned)fblktab.size());
                std::fflush(stdout);
                continue;
            }

            std::printf("fblock %u / %u: %s->",
                (unsigned)fblockno, (unsigned)fblktab.size(),
                ReportSize(fblktab[fblockno].length).c_str()
                 );
            std::fflush(stdout);

            // Copy the fblock (don't create reference), because
            // read_inode_and_blocks() may cause the fblock to be
            // removed from the cache while it's still in use.
            // Also, read it uncached so that we don't accidentally
            // cause the inode-table fblocks to be removed from cache.
            cromfs_cached_fblock fblock = read_fblock_uncached(fblockno);
            
            std::printf("%s; -> %u targets... ", ReportSize(fblock.size()).c_str(), nfiles);
            std::fflush(stdout);
            
            uint_fast64_t wrote_size = 0;

            /* Write into each file that requires data from this fblock */
            for(std::multimap<cromfs_fblocknum_t, cromfs_inodenum_t>::iterator
                i = fblock_users.find(fblockno);
                i != fblock_users.end() && i->first == fblockno;
                ++i)
            {
                const cromfs_inodenum_t inonum = i->second;
                const std::string& filename = inode_files.find(inonum)->second;
                /* ^ Find the first name. It doesn't matter which,
                 *   since if the files were hardlinked, any of them
                 *   can be written into affecting all simultaneously.
                 */
                
                //const cromfs_inodenum_t inonum = dir[i->second];
                const std::string target = targetdir + "/" + filename;
                const cromfs_inode_internal ino = read_inode_and_blocks(inonum);
                
                // std::printf("%s <- fblock %u...\n", i->second.c_str(), (unsigned)fblockno);
                
                /* Use O_NOATIME for some performance gain. If your libc
                 * does not support that flag, ignore it.
                 */
                int fd = open(target.c_str(), O_WRONLY | O_LARGEFILE
#ifdef O_NOATIME
                                                       | O_NOATIME
#endif
                              , 0666);
                if(fd < 0)
                {
                    perror(target.c_str());
                    continue;
                }
                
                FileOutputter file(fd, ino.bytesize);
                
                /* Find which blocks need this fblock */
                for(unsigned a=0; a<ino.blocklist.size(); ++a)
                {
                    if(ino.blocklist[a] >= blktab.size())
                    {
                        std::fprintf(stderr, "inode %u (used by %s) is corrupt (block #%u indicates block %llu, but block table has only %llu)\n",
                            (unsigned)inonum, filename.c_str(),
                            a,
                            (unsigned long long)ino.blocklist[a],
                            (unsigned long long)blktab.size()
                               );
                        continue;
                    }
                    const cromfs_block_storage& block = blktab[ino.blocklist[a]];
                    if(block.fblocknum != fblockno) continue;
                    
                    /* Allright, it uses data from this fblock. */

                    /* Count how much. */
                    uint_fast64_t block_size = sblock.uncompressed_block_size;
                    uint_fast64_t file_offset = a * block_size;
                    /* the last block may be smaller than the block size */
                    if(a+1 == ino.blocklist.size() && (ino.bytesize % block_size) > 0)
                    {
                        block_size = ino.bytesize % block_size;
                    }
                    
                    if(block.startoffs + block_size > fblock.size())
                    {
                        std::fprintf(stderr, "block %u (block #%u of inode %u) is corrupt (points to bytes %llu-%llu, fblock size is %llu)\n",
                            (unsigned)ino.blocklist[a],
                            a,
                            (unsigned)inonum,
                            (unsigned long long)(block.startoffs),
                            (unsigned long long)(block.startoffs + block_size-1),
                            (unsigned long long)fblock.size()
                                );
                        continue;
                    }
                    
                    /* Leave holes into the files (allowed by POSIX standard). */
                    /* When the block consists entirely of zeroes, it does
                     * not need to be written.
                     */
                    if(!use_sparse
                    || !is_zero_block(&fblock[block.startoffs], block_size))
                    {
                        file.write(&fblock[block.startoffs], block_size, file_offset);
                    }
                    wrote_size += block_size;
                }
                
                /* "file" goes out of scope, and hence it will be automatically closed. */
            }
            
            if(expect_size < wrote_size)
            {
                std::fprintf(stderr, "corrupt data: got more data than expected\n");
            }
            
            expect_size -= wrote_size;
            
            std::printf("%s extracted, %s to do.\n",
                ReportSize(wrote_size).c_str(),
                ReportSize(expect_size).c_str());
            
            total_written += wrote_size;
        }
        
        cache_fblocks.clear(); // save RAM
        
        std::printf("Total written: %s\n", ReportSize(total_written).c_str());
        
        std::printf("Fixing up symlinks and access timestamps\n");
        
        /* Reverse order so that dirs are touched after the files within.
         * This is something GNU tar can't do -- to extract modification
         * times of directories properly.
         */
        for(cromfs_dirinfo::reverse_iterator i = dir.rbegin(); i != dir.rend(); ++i)
        {
            if(duplicate_inodes.find(i->first) != duplicate_inodes.end()) continue;
            
            const cromfs_inodenum_t inonum = i->second;
            const cromfs_inode_internal ino = read_inode(inonum);
            const std::string target = targetdir + "/" + i->first;
            
            if(S_ISLNK(ino.mode))
            {
                std::vector<char> buffer(ino.bytesize + 1, '\0');
                
                int fd = open(target.c_str(), O_RDONLY | O_LARGEFILE);
                if(fd < 0)
                {
                    perror(target.c_str());
                }
                
                int nread = pread64(fd, &buffer[0], ino.bytesize, 0);
                close(fd);
                
                if(nread < ino.bytesize)
                {
                    std::fprintf(stderr, "%s: incomplete symlink\n",
                        target.c_str());
                    continue;
                }
                
                unlink(target.c_str());
                
                if(symlink(&buffer[0], target.c_str()) < 0)
                {
                    perror(target.c_str());
                }
            }
            
            if(use_sparse)
            {
                /* Ensure the entry is the right size (because we wrote sparse files) */
                if(S_ISREG(ino.mode)
                && truncate64( target.c_str(), ino.bytesize) < 0)
                {
                    perror(target.c_str());
                }
            }
            
            if(!S_ISLNK(ino.mode)
            && chmod( target.c_str(), ino.mode & 07777) < 0)
            {
                perror(target.c_str());
            }
            
            if(lchown( target.c_str(), ino.uid, ino.gid) < 0)
            {
                perror(target.c_str());
            }

            struct utimbuf data = { ino.time, ino.time };
            
            /* it is not possible to use utime() to change symlinks' modtime */
            if(!S_ISLNK(ino.mode)
            && utime(target.c_str(), &data) < 0)
                perror(target.c_str());
        }
        
        std::printf("Creating %u hardlinks...\n",
            (unsigned) (inode_files.size() - seen_inodes.size())
                   );
        for(std::map<cromfs_inodenum_t, std::string>::iterator
            i = seen_inodes.begin(); i != seen_inodes.end(); ++i)
        {
            cromfs_inodenum_t inonum   = i->first;
            const std::string& link_fn = i->second;
            
            for(std::multimap<cromfs_inodenum_t, std::string>::iterator
                j = inode_files.find(inonum);
                j != inode_files.end() && j->first == inonum;
                ++j)
            {
                if(j->second != link_fn)
                {
                    std::string oldpath = targetdir + "/" + link_fn;
                    std::string newpath = targetdir + "/" + j->second;
                    int r = link(oldpath.c_str(), newpath.c_str());
                    if(r < 0 && errno == EEXIST)
                    {
                        unlink( newpath.c_str() );
                        link(oldpath.c_str(), newpath.c_str());
                    }
                    if(r < 0)
                    {
                        perror(newpath.c_str());
                    }
                }
            }
        }
        cleanup();
    }

private:
    void ScanDirectories()
    {
        if(!simgraph_mode)
            std::printf("Scanning directories...\n");

        if(listing_mode)
        {
            printf("mode #fblocks uid/gid    size        datetime       name\n");
        }

        scan_dir_recursive(1, "");
    }
    
private:
    void scan_dir_recursive(cromfs_inodenum_t root_ino,
                            const std::string& parent)
    {
        cromfs_dirinfo& target = dir;
        
        cromfs_dirinfo thisdir = read_dir(root_ino, 0, (uint_fast32_t)~0U);
        for(cromfs_dirinfo::iterator i = thisdir.begin(); i != thisdir.end(); ++i)
        {
            std::string entname = parent + i->first;

            cromfs_inode_internal ino = read_inode_and_blocks(i->second);
            
            std::set<cromfs_fblocknum_t> fblist;
            for(unsigned a=0; a<ino.blocklist.size(); ++a)
                fblist.insert(blktab[ino.blocklist[a]].fblocknum);

            if(listing_mode)
            {
                if(MatchFile(entname))
                {
                    std::printf("%s%3u %u/%u %11llu %s %s\n",
                        TranslateMode(ino.mode).c_str(),
                        (unsigned)fblist.size(),
                        (unsigned)ino.uid,
                        (unsigned)ino.gid,
                        (unsigned long long)ino.bytesize,
                        DumpTime(ino.time).c_str(),
                        entname.c_str()
                               );
                }
            }
            
            bool namematch = MatchFile(entname);
            if(!S_ISDIR(ino.mode))
            {
                if(!namematch) continue;
            }
            
            bool is_duplicate_inode = inode_files.find(i->second) != inode_files.end();
            
            target[entname] = i->second;
            
            if(is_duplicate_inode && S_ISDIR(ino.mode))
            {
                std::fprintf(stderr, "Corrupt filesystem: two or more directories have the same inode number\n");
            }
            
            if(namematch || !simgraph_mode)
            {
                inode_files.insert(std::make_pair(i->second, entname));
            }
            
            if(S_ISDIR(ino.mode))
            {
                scan_dir_recursive(i->second, entname + "/");
            }
            else if(S_ISLNK(ino.mode) /* only symlinks and regular files have content */
                 || S_ISREG(ino.mode)) // to limit extraction to certain type
            {
                if(!is_duplicate_inode)
                {
                    /* List the fblocks needed by this file, but only
                     * for the first occurance to prevent duplicate
                     * writes into hardlinked files.
                     */
                    
                    for(std::set<cromfs_fblocknum_t>::iterator
                        j = fblist.begin(); j != fblist.end(); ++j)
                    {
                        fblock_users.insert(std::make_pair(*j, i->second));
                    }
                }
            }
            else if(ino.bytesize > 0)
            {
                std::fprintf(stderr, "inode %llu (%s) is corrupt. It has mode %0o (%s) but non-zero size %llu\n",
                    (unsigned long long)i->second,
                    entname.c_str(),
                    (unsigned)ino.mode,
                    TranslateMode(ino.mode).c_str(),
                    (unsigned long long)ino.bytesize);
            }
        }
    }
    
    rangeset<cromfs_block_index>
    create_rangeset(cromfs_inodenum_t inonum)
    {
        rangeset<cromfs_block_index> result;
        cromfs_inode_internal ino = read_inode_and_blocks(inonum);
        for(unsigned a=0; a<ino.blocklist.size(); ++a)
        {
            const cromfs_block_storage& blk = blktab[ino.blocklist[a]];

            /* Count how much. */
            uint_fast64_t block_size = sblock.uncompressed_block_size;
            /* the last block may be smaller than the block size */
            if(a+1 == ino.blocklist.size() && (ino.bytesize % block_size) > 0)
            {
                block_size = ino.bytesize % block_size;
            }

            cromfs_block_index b(blk);
            result.set(b, b + block_size);
        }
        return result;
    }
    
    const std::string FileNameToXML(const std::string& name)
    {
        std::string result;
        for(unsigned a=0; a<name.size(); ++a)
        {
            unsigned char ch = name[a];
            if(ch == '<') { result += "&lt;"; continue; }
            if(ch == '&') { result += "&amp;"; continue; }
            if(ch < ' ' || ch >= 0x7E
            || (ch==' ' && (a==0 || a+1 == name.size()))
              )
            {
                char Buf[16];
                sprintf(Buf, "&#%u;", ch);
                result += Buf;
            }
            else
                result += ch;
        }
        return result;
    }
};

int main(int argc, char** argv)
{
    std::string fsfile;
    std::string outpath;
    
    bool should_create_output = true;
    
    for(;;)
    {
        int option_index = 0;
        static struct option long_options[] =
        {
            {"help",        0, 0,'h'},
            {"version",     0, 0,'V'},
            {"list",        0, 0,'l'},
            {"nosparse",    0, 0,'s'},
            {"exclude",     1, 0,'x'},
            {"exclude-from",1, 0,'X'},
            {"simgraph",    0, 0,10001},
            {0,0,0,0}
        };
        int c = getopt_long(argc, argv, "hVlsx:X:", long_options, &option_index);
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
                    "unmkcromfs v"VERSION" - Copyright (C) 1992,2007 Bisqwit (http://iki.fi/bisqwit/)\n"
                    "\n"
                    "Extracts (the) contents of a cromfs image without mounting it.\n"
                    "\n"
                    "Usage: unmkcromfs [<options>] <source_image> <target_path> [<files> [<...>]]\n"
                    " --help, -h         This help\n"
                    " --version, -V      Displays version information\n"
                    " --list, -l         List contents without extracting files\n"
                    " --nosparse, -s     Do not leave holes in the files\n"
                    " --exclude, -x <pattern>\n"
                    "                    Exclude files matching <pattern> from the archive\n"
                    " --exclude-from, -X <file>\n"
                    "                    Exclude files matchig the patterns in <file>\n"
                    " --simgraph         Create a similarity graph of the contents of\n"
                    "                    the file system without extracting files\n"
                    "\n");
                return 0;
            }
            case 'l':
            {
                listing_mode  = true;
                should_create_output = false;
                break;
            }
            case 's':
            {
                use_sparse = false;
                break;
            }
            case 'x':
            {
                AddFilePattern(exclude_files, optarg);
                break;
            }
            case 'X':
            {
                AddFilePatternsFrom(exclude_files, optarg);
                break;
            }
            case 10001: // simgraph
            {
                simgraph_mode = true;
                should_create_output = false;
                break;
            }
        }
    }

    if(argc < optind+1)
    {
    ArgError:
        std::fprintf(stderr, "unmkcromfs: invalid parameters. See `unmkcromfs --help'\n");
        return 1;
    }
    fsfile  = argv[optind++];
    
    if(should_create_output)
    {
        if(argc < optind+1) goto ArgError;
        outpath = argv[optind++];
    }
    
    while(optind < argc)
    {
        AddFilePattern(extract_files, argv[optind++]);
    }
    
    if(should_create_output)
    {
        if(mkdir(outpath.c_str(), 0700) < 0)
        {
            if(errno != EEXIST)
            {
                perror(outpath.c_str());
            }
        }
    }
    
    int fd = open(fsfile.c_str(), O_RDONLY | O_LARGEFILE);
    if(fd < 0) { perror(fsfile.c_str()); return -1; }
    if(isatty(fd)) { std::fprintf(stderr, "input is a terminal. Doesn't work that way.\n");
                     return -1; }
    try
    {
        cromfs_decoder cromfs(fd);
        
        if(listing_mode)
            cromfs.do_listing();
        else if(simgraph_mode)
            cromfs.do_simgraph();
        else
            cromfs.do_extract(outpath.c_str());
    }
    catch(cromfs_exception e)
    {
        errno=e;
        perror("cromfs");
        return -1;
    }
    
    UnmatchedPatternListType unmatched = GetUnmatchedList(extract_files);
    if(!unmatched.empty())
    {
        for(UnmatchedPatternListType::const_iterator
            i = unmatched.begin();
            i != unmatched.end();
            ++i)
        {
            fprintf(stderr, "%s: Unmatched pattern\n",
                i->c_str());
        }
    }
}
