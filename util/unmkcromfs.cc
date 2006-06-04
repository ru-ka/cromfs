#include "../cromfs.hh"
#include "util.hh"

#include <unistd.h>
#include <fcntl.h>
#include <stdio.h>
#include <errno.h>
#include <utime.h>

#include <getopt.h>

#include <map>
#include <set>

static const std::string TranslateMode(unsigned mode)
{
    char mask[16] = "-.--.--.--";
    
    if(S_ISDIR(mode)) mask[0] = 'd';
    else if(S_ISCHR(mode)) mask[0] = 'c';
    else if(S_ISBLK(mode)) mask[0] = 'b';
    else if(S_ISFIFO(mode)) mask[0] = 'p';
    else if(S_ISLNK(mode)) mask[0] = 'l';
    else if(S_ISSOCK(mode)) mask[0] = 's';
    
    mask[1] = (mode & 0400) ? 'r' : '-';
    mask[2] = (mode & 0200) ? 'w' : '-';
    mask[3] = (mode & 0100) ? ((mode & 04000) ? 's' : 'x') : ((mode & 04000) ? 'S' : '-');
    mask[4] = (mode & 0040) ? 'r' : '-';
    mask[5] = (mode & 0020) ? 'w' : '-';
    mask[6] = (mode & 0010) ? ((mode & 02000) ? 's' : 'x') : ((mode & 02000) ? 'S' : '-');
    mask[7] = (mode & 0004) ? 'r' : '-';
    mask[8] = (mode & 0002) ? 'w' : '-';
    mask[9] = (mode & 0001) ? ((mode & 01000) ? 's' : 'x') : ((mode & 01000) ? 'S' : '-');
    
    return mask;
}
static const std::string DumpTime(uint_fast32_t time)
{
    const struct tm* tm = localtime((const time_t*)&time);
    char Buf[512] = "";
    strftime(Buf, sizeof(Buf)-1, "%Y-%m%d-%H%M%S", tm);
    return Buf;
}

class cromfs_decoder: public cromfs
{
private:
    bool listing_mode;
public:
    cromfs_decoder(int fd): cromfs(fd) { }
    
    void extract(const std::string& targetdir, bool list_only)
    {
        listing_mode = list_only;
        
        cromfs_dirinfo dir;
        std::multimap<cromfs_fblocknum_t, std::string> fblock_users;
        
        fprintf(stderr, "Scanning directories...\n");
        merge_dir_recursive(dir, fblock_users, 1, "");
        
        if(listing_mode) return;
        
        fprintf(stderr, "Creating %u inodes... (the files and directories)\n",
            (unsigned)dir.size()
               );

        umask(0);
        for(cromfs_dirinfo::iterator i = dir.begin(); i != dir.end(); ++i)
        {
            cromfs_inode_internal ino = read_inode(i->second);
            std::string target = targetdir + "/" + i->first;
            
            int r = 0;
            if(S_ISDIR(ino.mode))
                r = mkdir( target.c_str(), ino.mode | 0700);
            else if(S_ISLNK(ino.mode))
            {
                // Create symlinks as regular files first.
                // This allows to construct them in pieces.
                // When symlink() is called, the entire name must be known.
                r = mknod( target.c_str(), S_IFREG | 0600, 0);
            }
            else
                r = mknod( target.c_str(), ino.mode | 0600, ino.rdev);
            
            if(S_ISREG(ino.mode) || S_ISLNK(ino.mode))
            {
                // To help filesystem in optimizing the storage.
                // It does not matter if this call fails.
                truncate64( target.c_str(), ino.bytesize);
            }
            
            if(r < 0)
            {
                perror(target.c_str());
            }

            // Fix up permissions later. Now the important thing is to
            // be able to write into those files and directories.
            
            //printf("%s: %d\n", i->first.c_str(), (int) i->second);
        }
        
        uint_fast64_t total_written = 0;
        
        fprintf(stderr, "Writing files...\n");
        for(cromfs_fblocknum_t fblockno = 0; fblockno < fblktab.size(); ++fblockno)
        {
            unsigned nfiles = 0;

            for(std::multimap<cromfs_fblocknum_t, std::string>::iterator
                i = fblock_users.find(fblockno);
                i != fblock_users.end() && i->first == fblockno;
                ++i) ++nfiles;
            
            if(!nfiles)
            {
                // This fblock only contains directories
                // or parts of the inode table. Do not waste
                // resources decompressing it now.
                
                fprintf(stderr, "fblock %u / %u: skipping\n",
                    (unsigned)fblockno, (unsigned)fblktab.size());
                fflush(stderr);
                continue;
            }

            // Copy the fblock (don't create reference), because
            // read_inode_and_blocks() may cause the fblock to be
            // removed from the cache while it's still in use.
            cromfs_cached_fblock fblock = read_fblock(fblockno);

            cache_fblocks.clear(); // save RAM
            
            fprintf(stderr, "fblock %u / %u: %s->%s; writing -> %u files... ",
                (unsigned)fblockno, (unsigned)fblktab.size(),
                ReportSize(fblktab[fblockno].length).c_str(),
                ReportSize(fblock.size()).c_str(),
                nfiles);
            fflush(stderr);
            
            uint_fast64_t wrote_size = 0;

            for(std::multimap<cromfs_fblocknum_t, std::string>::iterator
                i = fblock_users.find(fblockno);
                i != fblock_users.end() && i->first == fblockno;
                ++i)
            {
                std::string target = targetdir + "/" + i->second;
                cromfs_inode_internal ino = read_inode_and_blocks(dir[i->second]);
                
                // fprintf(stderr, "%s <- fblock %u...\n", i->second.c_str(), (unsigned)fblockno);
                
                /* Use O_NOATIME for some performance gain. If your compiler
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
                
                for(unsigned a=0; a<ino.blocklist.size(); ++a)
                {
                    if(ino.blocklist[a] >= blktab.size())
                    {
                        fprintf(stderr, "inode %u is corrupt (block #%u indicates block %llu, but block table has only %llu)\n",
                            (unsigned)dir[i->second],
                            a,
                            (unsigned long long)ino.blocklist[a],
                            (unsigned long long)blktab.size()
                               );
                        continue;
                    }
                    const cromfs_block_storage& block = blktab[ino.blocklist[a]];
                    if(block.fblocknum != fblockno) continue;
                    
                    uint_fast64_t block_size = sblock.uncompressed_block_size;
                    uint_fast64_t file_offset = a * block_size;
                    if(a+1 == ino.blocklist.size()
                    && (ino.bytesize % block_size) > 0)
                        block_size = ino.bytesize % block_size;
                    
                    if(block.startoffs + block_size > fblock.size())
                    {
                        fprintf(stderr, "block %u (block #%u of inode %u) is corrupt (points to byte %llu, fblock size is %llu)\n",
                            (unsigned)ino.blocklist[a],
                            a,
                            (unsigned)dir[i->second],
                            (unsigned long long)block.startoffs,
                            (unsigned long long)fblock.size()
                                );
                        continue;
                    }
                    
                    pwrite64(fd, &fblock[block.startoffs], block_size, file_offset);
                    
                    wrote_size += block_size;
                }
                
                close(fd);
            }
            
            fprintf(stderr, "%s extracted.\n", ReportSize(wrote_size).c_str());
            
            total_written += wrote_size;
        }
        
        cache_fblocks.clear(); // save RAM
        
        fprintf(stderr, "Total written: %s\n", ReportSize(total_written).c_str());
        
        fprintf(stderr, "Fixing up symlinks and access timestamps\n");
        
        // reverse order so that dirs are touched after the files within
        for(cromfs_dirinfo::reverse_iterator i = dir.rbegin(); i != dir.rend(); ++i)
        {
            cromfs_inode_internal ino = read_inode(i->second);
            std::string target = targetdir + "/" + i->first;
            
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
                    fprintf(stderr, "%s: incomplete symlink\n",
                        target.c_str());
                    continue;
                }
                
                unlink(target.c_str());
                
                if(symlink(&buffer[0], target.c_str()) < 0)
                {
                    perror(target.c_str());
                }
            }
            
            if(!S_ISLNK(ino.mode)
            && chmod( target.c_str(), ino.mode & 07777) < 0)
            {
                perror(target.c_str());
            }
            
            if(chown( target.c_str(), ino.uid, ino.gid) < 0)
            {
                perror(target.c_str());
            }

            struct utimbuf data = { ino.time, ino.time};
            
            if(utime(target.c_str(), &data) < 0)
                perror(target.c_str());
        }
    }

private:
    void merge_dir_recursive(cromfs_dirinfo& target,
                             std::multimap<cromfs_fblocknum_t, std::string>& fblock_users,
                             cromfs_inodenum_t root_ino,
                             const std::string& parent)
    {
        cromfs_dirinfo dir = read_dir(root_ino, 0, (uint_fast32_t)~0U);
        for(cromfs_dirinfo::iterator i = dir.begin(); i != dir.end(); ++i)
        {
            std::string entname = parent + i->first;

            target[entname] = i->second;
            
            cromfs_inode_internal ino = read_inode_and_blocks(i->second);
            
            if(listing_mode)
            {
                printf("%s %u/%u %11llu %s %s\n",
                    TranslateMode(ino.mode).c_str(),
                    (unsigned)ino.uid,
                    (unsigned)ino.gid,
                    (unsigned long long)ino.bytesize,
                    DumpTime(ino.time).c_str(),
                    entname.c_str());
            }
            
            if(S_ISDIR(ino.mode))
            {
                merge_dir_recursive(target, fblock_users, i->second, entname + "/");
            }
            else if(S_ISLNK(ino.mode)
                 || S_ISREG(ino.mode)) // to limit extraction to certain type
            {   
                // list the fblocks needed by this file
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
    }
};

int main(int argc, char** argv)
{
    std::string fsfile;
    std::string outpath;
    
    bool list_only = false;
    
    for(;;)
    {
        int option_index = 0;
        static struct option long_options[] =
        {
            {"help",        0, 0,'h'},
            {"version",     0, 0,'V'},
            {"list",        0, 0,'l'},
            {0,0,0,0}
        };
        int c = getopt_long(argc, argv, "hVl", long_options, &option_index);
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
                    "Extracts the contents of a cromfs image without mounting it.\n"
                    "\n"
                    "Usage: unmkcromfs [<options>] <source_image> <target_path>\n"
                    " --help, -h         This help\n"
                    " --version, -V      Displays version information\n"
                    " --list, -l         List contents without extracting files\n"
                    "\n");
                return 0;
            }
            case 'l':
            {
                list_only = true;
                break;
            }
        }
    }

    if(argc != optind+2)
    {
        std::fprintf(stderr, "unmkcromfs: invalid parameters. See `unmkcromfs --help'\n");
        return 1;
    }
    
    fsfile  = argv[optind+0];
    outpath = argv[optind+1];
    
    if(mkdir(outpath.c_str(), 0700) < 0)
    {
        if(errno != EEXIST)
        {
            perror(outpath.c_str());
        }
    }
    
    int fd = open(fsfile.c_str(), O_RDONLY);
    if(fd < 0) { perror(fsfile.c_str()); return -1; }
    if(isatty(fd)) { fprintf(stderr, "input is a terminal. Doesn't work that way.\n");
                     return -1; }
    try
    {
        cromfs_decoder cromfs(fd);
        
        cromfs.extract(outpath.c_str(), list_only);
    }
    catch(cromfs_exception e)
    {
        errno=e;
        perror("cromfs");
        return -1;
    }
}
