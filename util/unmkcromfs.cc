#define _LARGEFILE64_SOURCE
#define __STDC_CONSTANT_MACROS

#include "../cromfs.hh"
#include "util.hh"

#include <unistd.h>
#include <fcntl.h>
#include <stdio.h>
#include <errno.h>
#include <utime.h>

#include <getopt.h>
#include <fnmatch.h>

#include <map>
#include <set>

static bool listing_mode  = false;
static bool use_sparse    = true;
static std::vector<std::string> extract_files;

static bool MatchFile(const std::string& entname)
{
    if(extract_files.empty()) return true;
    
    for(unsigned a=0; a<extract_files.size(); ++a)
    {
        if(fnmatch(
            extract_files[a].c_str(),
            entname.c_str(),
            0 
#if 0
            | FNM_PATHNAME
            /* disabled. It is nice if *Zelda* also matches subdir/Zelda. */
#endif
#ifdef FNM_LEADING_DIR
            | FNM_LEADING_DIR
            /* GNU extension which does exactly what I want --Bisqwit
             * With this, one can enter pathnames to the commandline and
             * those will too be extracted, without need to append / and *
             */
#endif
          ) == 0) return true;
    }
    return false;
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

class cromfs_decoder: public cromfs
{
public:
    cromfs_decoder(int fd): cromfs(fd) { }
    
    void extract(const std::string& targetdir)
    {
        cromfs_dirinfo dir;
        std::multimap<cromfs_fblocknum_t, std::string> fblock_users;
        std::multimap<cromfs_inodenum_t, std::string> inode_files;
        
        std::printf("Scanning directories...\n");
        merge_dir_recursive(dir, fblock_users, inode_files, 1, "");
        
        if(listing_mode) return;
        
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

            for(std::multimap<cromfs_fblocknum_t, std::string>::iterator
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
            for(std::multimap<cromfs_fblocknum_t, std::string>::iterator
                i = fblock_users.find(fblockno);
                i != fblock_users.end() && i->first == fblockno;
                ++i)
            {
                const cromfs_inodenum_t inonum = dir[i->second];
                const std::string target = targetdir + "/" + i->second;
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
                            (unsigned)inonum, i->second.c_str(),
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
                
                int fd = open(target.c_str(), O_RDONLY);
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
    }

private:
    void merge_dir_recursive(cromfs_dirinfo& target,
                             std::multimap<cromfs_fblocknum_t, std::string>& fblock_users,
                             std::multimap<cromfs_inodenum_t, std::string>& inode_files,
                             cromfs_inodenum_t root_ino,
                             const std::string& parent)
    {
        cromfs_dirinfo dir = read_dir(root_ino, 0, (uint_fast32_t)~0U);
        for(cromfs_dirinfo::iterator i = dir.begin(); i != dir.end(); ++i)
        {
            std::string entname = parent + i->first;

            cromfs_inode_internal ino = read_inode_and_blocks(i->second);
            
            if(listing_mode)
            {
                if(MatchFile(entname))
                {
                    std::printf("%s %u/%u %11llu %s %s\n",
                        TranslateMode(ino.mode).c_str(),
                        (unsigned)ino.uid,
                        (unsigned)ino.gid,
                        (unsigned long long)ino.bytesize,
                        DumpTime(ino.time).c_str(),
                        entname.c_str());
                }
            }
            
            if(!S_ISDIR(ino.mode))
            {
                if(!MatchFile(entname)) continue;
            }
            
            bool is_duplicate_inode = inode_files.find(i->second) != inode_files.end();
            
            target[entname] = i->second;
            
            if(is_duplicate_inode && S_ISDIR(ino.mode))
            {
                std::fprintf(stderr, "Corrupt filesystem: two or more directories have the same inode number\n");
            }
            
            inode_files.insert(std::make_pair(i->second, entname));
            
            if(S_ISDIR(ino.mode))
            {
                merge_dir_recursive(target, fblock_users, inode_files, i->second, entname + "/");
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
                    
                    std::set<cromfs_fblocknum_t> fblist;
                    for(unsigned a=0; a<ino.blocklist.size(); ++a)
                        fblist.insert(blktab[ino.blocklist[a]].fblocknum);

                    for(std::set<cromfs_fblocknum_t>::iterator
                        j = fblist.begin(); j != fblist.end(); ++j)
                    {
                        fblock_users.insert(std::make_pair(*j, entname));
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
};

int main(int argc, char** argv)
{
    std::string fsfile;
    std::string outpath;
    
    for(;;)
    {
        int option_index = 0;
        static struct option long_options[] =
        {
            {"help",        0, 0,'h'},
            {"version",     0, 0,'V'},
            {"list",        0, 0,'l'},
            {"nosparse",    0, 0,'s'},
            {0,0,0,0}
        };
        int c = getopt_long(argc, argv, "hVls", long_options, &option_index);
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
                    "unmkcromfs v"VERSION" - Copyright (C) 1992,2006 Bisqwit (http://iki.fi/bisqwit/)\n"
                    "\n"
                    "Extracts (the) contents of a cromfs image without mounting it.\n"
                    "\n"
                    "Usage: unmkcromfs [<options>] <source_image> <target_path> [<files> [<...>]]\n"
                    " --help, -h         This help\n"
                    " --version, -V      Displays version information\n"
                    " --list, -l         List contents without extracting files\n"
                    " --nosparse, -s     Do not leave holes in the files\n"
                    "\n");
                return 0;
            }
            case 'l':
            {
                listing_mode = true;
                break;
            }
            case 's':
            {
                use_sparse = false;
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
    
    if(!listing_mode)
    {
        if(argc < optind+1) goto ArgError;
    
        outpath = argv[optind++];
    }
    
    while(optind < argc)
    {
        extract_files.push_back(argv[optind++]);
    }
    
    if(!listing_mode)
    {
        if(mkdir(outpath.c_str(), 0700) < 0)
        {
            if(errno != EEXIST)
            {
                perror(outpath.c_str());
            }
        }
    }
    
    int fd = open(fsfile.c_str(), O_RDONLY);
    if(fd < 0) { perror(fsfile.c_str()); return -1; }
    if(isatty(fd)) { std::fprintf(stderr, "input is a terminal. Doesn't work that way.\n");
                     return -1; }
    try
    {
        cromfs_decoder cromfs(fd);
        cromfs.extract(outpath.c_str());
    }
    catch(cromfs_exception e)
    {
        errno=e;
        perror("cromfs");
        return -1;
    }
}
