#define _LARGEFILE64_SOURCE
#define __STDC_CONSTANT_MACROS

#include "../cromfs.hh"
#include "lib/fadvise.hh"
#include "lib/util.hh"
#include "lib/threadfun.hh"

#ifdef _OPENMP
# include <omp.h>
#endif
#include <set>

#include <unistd.h>
#include <fcntl.h>
#include <stdio.h>
#include <errno.h>
#include <utime.h>
#include <stdarg.h>
#include <cstring>

#ifdef HAS_LUTIMES
 #include <sys/time.h>
#endif
#include <sys/stat.h>

#include <getopt.h>

#include "fnmatch.hh"
#include "rangeset.hh"
#include "rangemultimap.hh"
#include "longfilewrite.hh"
#include "fsballocator.hh"

#include <map>
#include <set>

static unsigned UseThreads = 0;
static bool listing_mode  = false;
static bool simgraph_mode = false;
static bool use_sparse    = true;
static bool extract_paths = true;
static int verbose        = 0;
static MatchingFileListType extract_files;
static MatchingFileListType exclude_files;

static long BSIZE,FSIZE;

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

static const std::string GetTargetPath(const std::string& targetdir, const std::string& entname)
{
    std::string result = targetdir;
    result += '/';

    std::string::size_type p = entname.find('/');

    if(extract_paths || p == entname.npos)
        result += entname;
    else
        result += entname.substr(p+1);

    return result;
}

struct cromfs_block_index
{
private:
    uint_fast64_t value;
public:
    cromfs_block_index() : value(0) {}
    cromfs_block_index(uint_fast64_t v) : value(v) { }
    cromfs_block_index(const cromfs_block_internal& blk)
        : value ( ((uint_fast64_t)(blk.fblocknum) << UINT64_C(32)) | blk.startoffs )  { }

    cromfs_block_index
        operator+ (uint_fast64_t v) const { return value+v; }

    bool operator==(const cromfs_block_index& b) const { return value==b.value; }
    bool operator!=(const cromfs_block_index& b) const { return value!=b.value; }
    bool operator< (const cromfs_block_index& b) const { return value< b.value; }

    operator uint_fast64_t () const { return value; }

    cromfs_block_index
        operator- (cromfs_block_index v) const { return value - v; }
};

static struct ThreadSafeConsole
{
    void beginthread()
    {
    #ifdef _OPENMP
        ScopedLock lck(lock);
        beginline();
    #endif
    }
    void endthread(
    #ifdef _OPENMP
        int threadno = omp_get_thread_num()
    #else
        int = 0
    #endif
                  )
    {
    #ifdef _OPENMP
        ScopedLock lck(lock);
        lines.erase(threadno);
    #endif
    }
    void printf(const char* fmt, ...)
    {
        va_list ap;
        va_start(ap, fmt);
        DoPrint(false, stdout, ap, fmt);
        va_end(ap);
    }
    void oneliner(const char* fmt, ...)
    {
        va_list ap;
        va_start(ap, fmt);
        DoPrint(true, stdout, ap, fmt);
        va_end(ap);
    }
    void erroroneliner(const char* fmt, ...)
    {
        va_list ap;
        va_start(ap, fmt);
        DoPrint(true, stderr, ap, fmt);
        va_end(ap);
    }
    ThreadSafeConsole()
    #ifdef _OPENMP
        : curline(0), curx(0),
          freeline(0), n_oneliners(0),
          lines(), lock()
    #else
        : GoingLine(false), DidOneliners(false)
    #endif
    {
    }
private:
    void DoPrint(bool OneLiner, std::FILE* target, va_list ap, const char* fmt)
    {
    #ifdef _OPENMP
        char Buf[4096];
        using namespace std; // vsnprintf may or may not be defined in std::
      #ifdef HAS_VSNPRINTF
        int size = vsnprintf(Buf, sizeof Buf, fmt, ap);
      #else
        int size = std::vsprintf(Buf, fmt, ap);
      #endif
        ScopedLock lck(lock);
        int threadno = omp_get_thread_num();
        if(OneLiner)
        {
            threadno += 10000;

            // Try to scroll the screen down to make room for another
            // oneliner, but don't scroll if it causes the upmost
            // non-oneliners to disappear

            int dest_line = freeline + std::min(24 - required_size(), n_oneliners);
            if(dest_line > freeline)
                put_cursor_to(dest_line);

            beginline(threadno);
        }
        doprint(target, threadno, Buf, size);
        if(OneLiner)
        {
            endthread(threadno);
            --freeline;
            ++n_oneliners;
        }
    #else
        // No OpenMP

        if(OneLiner)
        {
            if(GoingLine && !DidOneliners)
            {
                std::fputc('\n', target);
            }
            DidOneliners = true;
        }
        else
        {
            if(GoingLine && DidOneliners)
                std::fprintf(target, "... ");
            const char* nlpos = std::strrchr(fmt, '\n');
            GoingLine = nlpos ? nlpos[1] != '\0' : *fmt != '\0';
            DidOneliners = false;
        }

        std::vfprintf(target, fmt, ap);
    #endif
        std::fflush(target);
    }
    #ifdef _OPENMP
    void put_cursor_to(int y, int x=-1)
    {
        std::fflush(stdout); std::fflush(stderr);

        std::FILE* target = stdout;
        if(curline < y)
        {
            while(curline < y)
                { std::fputc('\n', target); ++curline; }
            curx=0;
        }
        else if(curline > y)
        {
            std::fprintf(target, "\33[%dA", curline-y); // go up
            curline = y;
        }
        if(x != -1)
        {
            if(curx < x)
                { std::fprintf(target, "\33[%dC", x-curx); curx = x; } // go right
            else if(x == 0 && curx > 0)
                { std::fputc('\r', target); curx = 0; } // go left
            else if(curx > x)
                { std::fprintf(target, "\33[%dD", curx-x); curx = x; } // go left
        }
        std::fflush(stdout); std::fflush(stderr);
    }
    void beginline(int threadno = omp_get_thread_num())
    {
        put_cursor_to(freeline);

        lines[threadno] = std::pair<int,int> (freeline++, 0);
        std::printf("\33[1L"); std::fflush(stdout);
    }
    void doprint(std::FILE* target, int threadno, const char* Buf, int size)
    {
    refind_cursor:
        if(!size) return;

        std::pair<int,int>& cursor = lines[threadno];
        int& lineno = cursor.first;
        int& xcoord = cursor.second;
        put_cursor_to(lineno, (*Buf != '\r' && *Buf != '\n') ? xcoord : -1);
        while(size > 0)
        {
            const char c = *Buf++; --size;
            std::fputc(c, target);
            if(c == '\n')
            {
                ++curline; curx = xcoord = 0;
                if(size > 0) beginline(threadno);
                goto refind_cursor;
            }
            if(c == '\r') { curx=0; continue; }
            if(c == '\t') { curx=(curx+8)&~7; continue; }
            ++curx;
            //if(curx >= 78) { std::putc('\b', target); --curx; }
        }
        xcoord = curx;
    }
    // Returns the number of lines that must currently be viewable
    int required_size() const
    {
        int res = 0;
        for(std::map<int/*threadno*/, std::pair<int,int> >::const_iterator
            i = lines.begin(); i != lines.end(); ++i)
        {
            int diff = freeline - i->second.first;
            if(diff > res) res = diff;
        }
        return res;
    }
    #endif
private:
    #ifdef _OPENMP
    int curline, curx;
    int freeline, n_oneliners;
    std::map<int/*threadno*/, std::pair<int,int> > lines;
    MutexType lock;
    #else
    bool GoingLine, DidOneliners;
    #endif
} ThreadSafeConsole;

class cromfs_decoder: public cromfs
{
private:
    std::multimap<cromfs_fblocknum_t, cromfs_inodenum_t> fblock_users;
    std::multimap<cromfs_inodenum_t, std::string> inode_files;

    /* List of files we want to extract. */
    cromfs_dirinfo dir;

    /* List of directories within "dir", that we don't yet
     * know whether we actually want to extract.
     */
    std::set<cromfs_inodenum_t> extra_dirs;
public:
    cromfs_decoder(int fd)
        : cromfs(fd),
          fblock_users(), inode_files(), dir(), extra_dirs() // -Weffc++
    {
        cromfs::Initialize();

        BSIZE = sblock.bsize;
        FSIZE = sblock.fsize;

        if(verbose >= 3)
        {
            std::printf(
                "Superblock signature %"LL_FMT"X\n"
                "BlockTab at 0x%"LL_FMT"X\n"
                "FBlkTab at 0x%"LL_FMT"X\n"
                "inotab at 0x%"LL_FMT"X (size 0x%"LL_FMT"X)\n"
                "rootdir at 0x%"LL_FMT"X (size 0x%"LL_FMT"X)\n"
                "FSIZE %u  BSIZE %u\n"
                "%u fblocks, %u blocks\n"
                "%"LL_FMT"u bytes of files\n",
                (unsigned long long)sblock.sig,
                (unsigned long long)sblock.blktab_offs,
                (unsigned long long)sblock.fblktab_offs,
                (unsigned long long)sblock.inotab_offs,  (unsigned long long)sblock.inotab_size,
                (unsigned long long)sblock.rootdir_offs, (unsigned long long)sblock.rootdir_size,
                (unsigned)FSIZE,
                (unsigned)BSIZE,
                (unsigned)fblktab.size(),
                (unsigned)blktab.size(),
                (unsigned long long)sblock.bytes_of_files
            );
        }
    }

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
        std::printf("  <total_size>%"LL_FMT"u</total_size>\n", (unsigned long long)sblock.bytes_of_files);
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
                std::printf("  <inode id=\"%"LL_FMT"u\">", (unsigned long long)prev);
                inodelist.push_back(prev);
            }
            std::printf("<file>%s</file>",
                FileNameToXML(i->second).c_str());
        }
        if(!first) std::printf("</inode>\n");
        std::printf(" </inodes>\n");

        std::printf(" <matches>\n");
        std::fflush(stdout);

        typedef std::set<cromfs_inodenum_t> handled_inodes_t;

        /* Create a map of which inodes cover what sections of the filesystem */
        rangemultimap<cromfs_block_index, cromfs_inodenum_t, FSBAllocator<int> > range_map;
        for(unsigned a=0; a<inodelist.size(); ++a)
        {
            const cromfs_inodenum_t ino_a = inodelist[a];
            const rangeset<cromfs_block_index, FSBAllocator<int> >
                range_a = create_rangeset(ino_a);
            for(rangeset<cromfs_block_index, FSBAllocator<int> >::const_iterator
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

            const rangeset<cromfs_block_index, FSBAllocator<int>
              >& range_a = range_map.get_rangelist(ino_a);

            handled_inodes_t candidates;

            /* Get the list of all inodes that coincide
             * with the ranges occupied by this inode
             */
            for(rangeset<cromfs_block_index, FSBAllocator<int> >::const_iterator
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

                const rangeset<cromfs_block_index, FSBAllocator<int> >& range_b = range_map.get_rangelist(ino_b);
                rangeset<cromfs_block_index, FSBAllocator<int> > intersect = range_a.intersect(range_b);
                if(intersect.empty()) continue;

                uint_fast64_t intersecting_size = 0;
                for(rangeset<cromfs_block_index, FSBAllocator<int> >::const_iterator
                    i = intersect.begin();
                    i != intersect.end();
                    ++i)
                {
                    intersecting_size += i->upper - i->lower;
                }

                std::printf("  <match inode1=\"%"LL_FMT"u\" inode2=\"%"LL_FMT"u\">",
                    (unsigned long long)ino_a,
                    (unsigned long long)ino_b);

                /*
                std::printf("<!-- asize %"LL_FMT"u, bsize %"LL_FMT"u -->\n",
                    size_a, size_b);*/

                std::printf("<bytes>%"LL_FMT"u</bytes><ratio>%.10f",
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

    bool IsFirstOccuranceOfInodenum(const cromfs_inodenum_t inonum,
                                    const std::string& entname) const
    {
        std::multimap<cromfs_inodenum_t, std::string>::const_iterator
            j = inode_files.find(inonum);
        if(j != inode_files.end())
        {
            if(j->second != entname)
            {
                return false;
            }
        }
        return true;
    }

    void FixupSymlinksAndOwnerships(const std::string& targetdir)
    {
        for(cromfs_dirinfo::const_iterator i = dir.begin(); i != dir.end(); ++i)
        {
            const cromfs_inodenum_t inonum = i->second;

            if(!IsFirstOccuranceOfInodenum(inonum, i->first))
                continue;

            const cromfs_inode_internal ino = read_inode(inonum);
            const std::string target = GetTargetPath(targetdir, i->first);

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

                if(nread < 0 || (uint_fast64_t)nread < ino.bytesize)
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

            /* permission bits are not defined for symlinks */

            if(!S_ISLNK(ino.mode)
            && chmod( target.c_str(), ino.mode & 07777) < 0)
            {
                perror(target.c_str());
            }

            if(lchown( target.c_str(), ino.uid, ino.gid) < 0)
            {
                perror(target.c_str());
            }
        }
    }

    void FixupHardlinks(const std::string& targetdir)
    {
        for(std::multimap<cromfs_inodenum_t, std::string>::const_iterator
            i = inode_files.begin(); i != inode_files.end(); )
        {
            const cromfs_inodenum_t inonum = i->first;
            const std::string& link_fn     = i->second;

            for(++i; i != inode_files.end() && i->first == inonum; ++i)
            {
                std::string oldpath = GetTargetPath(targetdir, link_fn);
                std::string newpath = GetTargetPath(targetdir, i->second);
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

    void FixupModificationTimes(const std::string& targetdir)
    {
        /* Reverse order so that dirs are touched after the files within.
         * This is something GNU tar can't do -- to extract modification
         * times of directories properly.
         */
        for(cromfs_dirinfo::reverse_iterator i = dir.rbegin(); i != dir.rend(); ++i)
        {
            const cromfs_inodenum_t inonum = i->second;

            if(!IsFirstOccuranceOfInodenum(inonum, i->first))
                continue;

            const cromfs_inode_internal ino = read_inode(inonum);
            const std::string target = GetTargetPath(targetdir, i->first);

        #ifdef HAS_LUTIMES
            struct timeval tv[2] = { { ino.time, 0 }, { ino.time, 0 } };
            if(lutimes(target.c_str(), tv) < 0)
            {
                if(!S_ISLNK(ino.mode) && errno == ENOSYS) goto try_utime;
                perror(target.c_str());
            }
            continue;
        #endif
        try_utime:;
            struct utimbuf data = { ino.time, ino.time };

            /* Note: It is not possible to use utime() to change symlinks' modtime */
            /* Note: utime() cannot change the "change time" either. */
            if(!S_ISLNK(ino.mode)
            && utime(target.c_str(), &data) < 0)
                perror(target.c_str());
        }
    }

    void do_extract(const std::string& targetdir)
    {
        cleanup();

        ScanDirectories();

        if(verbose >= 1)
        {
            std::printf("Creating %u inodes... (the files and directories)\n",
                (unsigned)dir.size()
                   );
        }

        uint_fast64_t expect_size = 0;
        size_t expect_files = 0;
        size_t expect_links = 0;
        size_t expect_inodes = 0;

        umask(0);
        for(cromfs_dirinfo::iterator i = dir.begin(); i != dir.end(); ++i)
        {
            const cromfs_inodenum_t inonum = i->second;

            std::string target = GetTargetPath(targetdir, i->first);

            if(!IsFirstOccuranceOfInodenum(inonum, i->first))
            {
                /* Don't create the inode twice */
                continue;
            }
            ++expect_inodes;

            const cromfs_inode_internal ino = read_inode(inonum);

            if(verbose >= 2) ThreadSafeConsole.oneliner("\t%s\n", target.c_str());
            int r = 0;
            if(S_ISDIR(ino.mode))
            {
                r = mkdir( target.c_str(), ino.mode | 0700);
            }
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

        if(verbose >= 1)
        {
            std::printf("Writing files... (%s in %u files and %u links)\n",
                ReportSize(expect_size).c_str(),
                (unsigned)expect_files,
                (unsigned)expect_links
                       );
        }

        /* Note: Using "long" for loop iteration variable, because OpenMP
         * requires the loop iteration variable to be of _signed_ type,
         * and cromfs_fblocknum_t is unsigned.
         */
      #pragma omp parallel for reduction(+:total_written)
        for(long/*cromfs_fblocknum_t*/ fblocknum=0; fblocknum<(long)fblktab.size(); ++fblocknum)
        {
            do_extract(fblocknum, targetdir, expect_size, total_written);
        }

        fblock_cache.clear(); // save RAM

        if(verbose >= 1)
            std::printf("Total written: %s\n", ReportSize(total_written).c_str());

        if(verbose >= 1)
            std::printf("Fixing up symlinks and ownerships\n");

        FixupSymlinksAndOwnerships(targetdir);

        if(verbose >= 1)
        {
            std::printf("Creating %u hardlinks...\n",
                (unsigned) (inode_files.size() - expect_inodes)
                       );
        }

        FixupHardlinks(targetdir);

        if(verbose >= 1)
            std::printf("Fixing up timestamps\n");

        FixupModificationTimes(targetdir);

        cleanup();
    }

    void do_extract(const cromfs_fblocknum_t fblocknum,
                    const std::string& targetdir,
                    uint_fast64_t& expect_size,
                    uint_fast64_t& total_written) const
    {
        ThreadSafeConsole.beginthread();

        /* Count the number of files/symlinks that require data
         * from this particular fblock.
         */
        unsigned nfiles = 0;

        for(std::multimap<cromfs_fblocknum_t, cromfs_inodenum_t>::const_iterator
            i = fblock_users.find(fblocknum);
            i != fblock_users.end() && i->first == fblocknum;
            ++i) ++nfiles;

        if(!nfiles)
        {
            /* This fblock only contains directories or parts of the
             * inode table, or none of the selected files.
             * Do not waste resources decompressing it now.
             */
            if(verbose >= 1)
            {
                ThreadSafeConsole.oneliner("fblock %u / %u: skipping\n",
                    (unsigned)fblocknum, (unsigned)fblktab.size());
            }

            FadviseDontNeed(fd, fblktab[fblocknum].filepos,
                                fblktab[fblocknum].length);

            ThreadSafeConsole.endthread();
            return;
        }

        if(verbose >= 1)
        {
            ThreadSafeConsole.printf("fblock %u / %u: %s->",
                (unsigned)fblocknum, (unsigned)fblktab.size(),
                ReportSize(fblktab[fblocknum].length).c_str()
                 );
        }

        // Copy the fblock (don't create reference), because
        // read_inode_and_blocks() may cause the fblock to be
        // removed from the cache while it's still in use.
        // Also, read it uncached so that we don't accidentally
        // cause the inode-table fblocks to be removed from cache.
        cromfs_cached_fblock fblock = read_fblock_uncached(fblocknum);

        FadviseDontNeed(fd, fblktab[fblocknum].filepos,
                            fblktab[fblocknum].length);

        if(fblocknum+1 < fblktab.size())
        {
            // At background, initiate reading for the next fblock.
            FadviseWillNeed(fd, fblktab[fblocknum+1].filepos,
                                fblktab[fblocknum+1].length);
        }

        if(verbose >= 1)
        {
            ThreadSafeConsole.printf("%s; -> %u targets... ", ReportSize(fblock.size()).c_str(), nfiles);
        }

        uint_fast64_t wrote_size = 0;

        /* Write into each file that requires data from this fblock */
        for(std::multimap<cromfs_fblocknum_t, cromfs_inodenum_t>::const_iterator
            i = fblock_users.find(fblocknum);
            i != fblock_users.end() && i->first == fblocknum;
            ++i)
        {
            const cromfs_inodenum_t inonum = i->second;
            const std::string& filename = inode_files.find(inonum)->second;
            /* ^ Find the first name. It doesn't matter which,
             *   since if the files were hardlinked, any of them
             *   can be written into affecting all simultaneously.
             */

            //const cromfs_inodenum_t inonum = dir[i->second];
            const std::string target = GetTargetPath(targetdir, filename);

            const cromfs_inode_internal ino =
                (const_cast<cromfs_decoder&>(*this)).
                read_inode_and_blocks(inonum);

            if(verbose >= 2)
            {
                ThreadSafeConsole.oneliner("\t[%u] %s\n",
                    (unsigned) fblocknum,
                    target.c_str()/*, (unsigned)fblocknum*/);
            }

            try
            {
                FileOutputter file(target.c_str(), ino.bytesize);

                const uint_fast64_t block_size = ino.blocksize;

                /* Find which blocks need this fblock */
                for(unsigned a=0; a<ino.blocklist.size(); ++a)
                {
                    if(ino.blocklist[a] >= blktab.size())
                    {
                        ThreadSafeConsole.erroroneliner("inode %u (used by %s) is corrupt (block #%u indicates block %"LL_FMT"u, but block table has only %"LL_FMT"u)\n",
                            (unsigned)inonum, filename.c_str(),
                            a,
                            (unsigned long long)ino.blocklist[a],
                            (unsigned long long)blktab.size()
                               );
                        continue;
                    }
                    const cromfs_block_internal& block = blktab[ino.blocklist[a]];

                    const uint_fast32_t block_fblocknum = block.fblocknum;
                    const uint_fast32_t block_startoffs = block.startoffs;

                    // Only write data from this fblock that is being handled now.
                    if(block_fblocknum != fblocknum) continue;

                    /* Allright, it uses data from this fblock. */

                    /* Count how much. */
                    uint_fast64_t file_offset = a * block_size;
                    uint_fast64_t read_size = block_size;

                    /* the last block may be smaller than the block size */
                    if(a+1 == ino.blocklist.size())
                        read_size = ino.bytesize - (ino.blocklist.size()-1) * block_size;

                    if(block_startoffs + read_size > fblock.size())
                    {
                        ThreadSafeConsole.erroroneliner("block %u (block #%u/%u of inode %u, %"LL_FMT"u/%"LL_FMT"u bytes) is corrupt (points to bytes %"LL_FMT"u-%"LL_FMT"u, fblock %u size is %"LL_FMT"u)\n",
                            (unsigned)ino.blocklist[a],
                            a, (unsigned)ino.blocklist.size(),
                            (unsigned)inonum,
                            (unsigned long long)read_size,
                            (unsigned long long)ino.bytesize,
                            (unsigned long long)(block_startoffs),
                            (unsigned long long)(block_startoffs + read_size-1),
                            (unsigned)fblocknum,
                            (unsigned long long)fblock.size()
                                );
                        continue;
                    }

                    file.write(&fblock[block_startoffs], read_size, file_offset, use_sparse);
                    wrote_size += read_size;
                }

                /* "file" goes out of scope, and hence it will be automatically closed. */
            }
            catch(int)
            {
                perror(target.c_str());
            }
        }

        if(expect_size < wrote_size)
        {
            ThreadSafeConsole.erroroneliner("corrupt data: got more data than expected from fblock %u\n", (unsigned)fblocknum);
        }

      #pragma omp atomic
        expect_size -= wrote_size;

        if(verbose >= 1)
        {
            //if(verbose >= 2) ThreadSafeConsole.printf("\n... "); // newline because files were listed
            ThreadSafeConsole.printf("remain-%s=%s.\n",
                ReportSize(wrote_size).c_str(),
                ReportSize(expect_size).c_str());
        }

      #pragma omp atomic
        total_written += wrote_size;

        FileOutputFlushAll();

        ThreadSafeConsole.endthread();
    }

private:
    void ScanDirectories()
    {
        if(!simgraph_mode && verbose >= 1)
            std::printf("Scanning directories...\n");

        if(listing_mode && verbose >= 1)
        {
            if(verbose >= 2) std::printf("inonum ");
            printf("mode #fblocks uid/gid    size        datetime       name\n");
        }

        extra_dirs.clear();
        scan_dir_recursive(1, "");

        /* Remove extra dirs. */
        for(cromfs_dirinfo::iterator j,i = dir.begin(); i != dir.end(); i=j)
        {
            cromfs_inodenum_t inonum = i->second;
            const std::string& name = i->first;

            j=i; ++j;

            if(extra_dirs.find(inonum) != extra_dirs.end())
            {
                /* This directory might not be required. Check carefully. */
                /* Check each entry in "dir". If they have the name of this
                 * entry in their path, and are not extras, this is required.
                 */
                bool necessary = false;
                if(extract_paths)
                {
                    const std::string path_stub = name + "/";

                    for(cromfs_dirinfo::iterator j = dir.lower_bound(path_stub);
                        j != dir.end(); ++j)
                    {
                        /*
                        fprintf(stderr, "compare(%s)\n"
                                        "       (%s)\n",
                                          j->first.c_str(),
                                          name.c_str());
                        */
                        if(j->first.compare(0, path_stub.size(), path_stub) != 0)
                        {
                            /* Does not match the path */
                            break;
                        }

                        if(extra_dirs.find(j->second) == extra_dirs.end())
                        {
                            necessary = true;
                            break;
                        }
                    }
                }
                if(!necessary)
                {
                    if(verbose >= 4)
                        printf("Ignoring dir: %s\n", name.c_str());
                    dir.erase(i);
                }
            }
        }
        extra_dirs.clear();
    }

private:
    void ListingDisplayInode(
        const std::string& entname,
        const cromfs_inodenum_t inonum,
        const cromfs_inode_internal& ino,
        const std::set<cromfs_fblocknum_t>& fblist,
        const std::string& linkdetail = "") const
    {
        if(verbose >= 1)
        {
            if(verbose >= 2) std::printf("%6u ", (unsigned)inonum);
            std::printf("%s%3u %u/%-3u %11"LL_FMT"u %s %s%s%s\n",
                TranslateMode(ino.mode).c_str(),
                (unsigned)fblist.size(),
                (unsigned)ino.uid,
                (unsigned)ino.gid,
                (unsigned long long)ino.bytesize,
                DumpTime(ino.time).c_str(),
                entname.c_str(),
                linkdetail.empty() ? "" : " -> ",
                linkdetail.c_str()
                       );
        }
        else if(verbose >= 0)
            std::printf("%s\n", entname.c_str());

        if(verbose >= 4 && !ino.blocklist.empty())
        {
            std::printf("  [blocks(%lu):", (unsigned long)ino.blocksize);
            for(size_t a=0; a<ino.blocklist.size(); ++a)
                std::printf(" %u", ino.blocklist[a]);
            std::printf("]\n");
        }
        if(verbose >= 3 && !ino.blocklist.empty())
        {
            std::set<cromfs_fblocknum_t> fset;
            for(size_t a=0; a<ino.blocklist.size(); ++a)
                fset.insert(blktab[ino.blocklist[a]].fblocknum);
            std::printf("  [fblocks accessed:");
            for(std::set<cromfs_fblocknum_t>::const_iterator
                i = fset.begin(); i != fset.end(); ++i)
                std::printf(" %u", (unsigned)*i);
            std::printf("]\n");
        }
    }

    void scan_dir_recursive(cromfs_inodenum_t root_ino,
                            const std::string& parent)
    {
        cromfs_dirinfo& target = dir;

        cromfs_dirinfo thisdir = read_dir(root_ino, 0, (uint_fast32_t)~0U);

        if(root_ino == 1 && listing_mode && verbose >= 0)
        {
            const std::string entname = "/";

            const cromfs_inodenum_t inonum = root_ino;
            cromfs_inode_internal ino = read_inode_and_blocks(inonum);
            std::set<cromfs_fblocknum_t> fblist;
            for(unsigned a=0; a<ino.blocklist.size(); ++a)
                fblist.insert(blktab[ino.blocklist[a]].fblocknum);

            ListingDisplayInode(entname, inonum, ino, fblist);
        }

        cromfs_dirinfo subdirs;
        for(cromfs_dirinfo::iterator i = thisdir.begin(); i != thisdir.end(); ++i)
        {
            cromfs_inodenum_t inonum = i->second;
            std::string entname = parent + i->first;

            //fprintf(stderr, "Reading(%s)(%ld)\n", entname.c_str(),(long)inonum);

            cromfs_inode_internal ino = read_inode_and_blocks(inonum);

            std::set<cromfs_fblocknum_t> fblist;
            for(unsigned a=0; a<ino.blocklist.size(); ++a)
                if(ino.blocklist[a] < blktab.size())
                    fblist.insert(blktab[ino.blocklist[a]].fblocknum);
                else
                {
                    fprintf(stderr, "Inode %"LL_FMT"u (%s) is corrupt. It refers to block %u which does not exist (%u blocks exist).\n",
                        (unsigned long long)inonum, entname.c_str(),
                        (unsigned) ino.blocklist[a],
                        (unsigned) blktab.size());
                    break;
                }

            bool namematch = MatchFile(entname);

            if(namematch && listing_mode)
            {
                std::string link_detail;

                if(verbose >= 1 && S_ISLNK(ino.mode))
                {
                    std::vector<unsigned char> linktargetbuffer(ino.bytesize);
                    read_file_data(ino, 0, &linktargetbuffer[0], ino.bytesize, entname.c_str());
                    link_detail.assign(
                        (const char*)&linktargetbuffer[0],
                        (const char*)&linktargetbuffer[ino.bytesize]);
                }

                ListingDisplayInode(entname, inonum, ino, fblist, link_detail);
            }

            if(inonum == 1) continue;

            if(!namematch)
            {
                /* Not a desired file? Ignore it. */
                if(!S_ISDIR(ino.mode)) continue;
                /* If it was a directory, let's first check if it's a
                 * path component of one of the desired files before
                 * ignoring it.
                 * Also, we need to traverse it anyway to see if any
                 * of the subdirectories are interesting.
                 */
                extra_dirs.insert(inonum);
            }

            bool is_duplicate_inode = inode_files.find(inonum) != inode_files.end();

            target[entname] = inonum;

            if(is_duplicate_inode && S_ISDIR(ino.mode))
            {
                std::fprintf(stderr, "Corrupt filesystem: two or more directories have the same inode number\n");
            }

            if(namematch || !simgraph_mode)
            {
                inode_files.insert(std::make_pair(inonum, entname));
            }

            if(S_ISDIR(ino.mode))
            {
                subdirs[entname + "/"] = inonum;
            }
            else if(S_ISLNK(ino.mode) /* only symlinks and regular files have content */
                 || S_ISREG(ino.mode)) // to limit extraction to certain type
            {
                if(!is_duplicate_inode)
                {
                    /* List the fblocks needed by this file, but only
                     * for the first occurance to prevent duplicate
                     * writes into hardlinked files.
                     *
                     * Also, S_ISDIR is excluded here, because directories
                     * are already being handled; they won't be extracted
                     * with the sparse completion algorithm.
                     */

                    for(std::set<cromfs_fblocknum_t>::iterator
                        j = fblist.begin(); j != fblist.end(); ++j)
                    {
                        fblock_users.insert(std::make_pair(*j, inonum));
                    }
                }
            }
            else if(ino.bytesize > 0)
            {
                std::fprintf(stderr, "inode %"LL_FMT"u (%s) is corrupt. It has mode %0o (%s) but non-zero size %"LL_FMT"u\n",
                    (unsigned long long)inonum,
                    entname.c_str(),
                    (unsigned)ino.mode,
                    TranslateMode(ino.mode).c_str(),
                    (unsigned long long)ino.bytesize);
            }
        }
        thisdir.clear();

        for(cromfs_dirinfo::const_iterator i = subdirs.begin(); i != subdirs.end(); ++i)
            scan_dir_recursive(i->second, i->first);
    }

    const rangeset<cromfs_block_index, FSBAllocator<int> >
    create_rangeset(cromfs_inodenum_t inonum)
    {
        rangeset<cromfs_block_index, FSBAllocator<int> > result;
        cromfs_inode_internal ino = read_inode_and_blocks(inonum);

        const uint_fast64_t block_size = ino.blocksize;

        for(unsigned a=0; a<ino.blocklist.size(); ++a)
        {
            const cromfs_block_internal& blk = blktab[ino.blocklist[a]];

            /* Count how much. */
            uint_fast64_t size = block_size;
            /* the last block may be smaller than the block size */
            if(a+1 == ino.blocklist.size())
                size = ino.bytesize - (ino.blocklist.size()-1) * block_size;

            cromfs_block_index b(blk);
            result.set(b, b + size);
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
            {"verbose",     0, 0,'v'},
            {"flat",        0, 0,'f'},
            {"threads",     1, 0,4003},
            {0,0,0,0}
        };
        int c = getopt_long(argc, argv, "hVlsx:X:vf", long_options, &option_index);
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
                    "unmkcromfs v"VERSION" - Copyright (C) 1992,2009 Bisqwit (http://iki.fi/bisqwit/)\n"
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
                    " --verbose, -v      Increase verbosity\n"
                    " --flat, -f         Do not extract with paths\n"
                    "                    (Watch out for identical filenames)\n"
                    " --threads <value>  Use the given number of threads.\n"
                    "                    The threads will be used when extracting files.\n"
                    "                    Use 0 or 1 to disable threads. (Default)\n"
                    "\n");
                return 0;
            }
            case 'l':
            {
                listing_mode = true;
                should_create_output = false;
                break;
            }
            case 'v':
            {
                ++verbose;
                break;
            }
            case 's':
            {
                use_sparse = false;
                break;
            }
            case 'f':
            {
                extract_paths = false;
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
            case 4003: // threads
            {
                char* arg = optarg;
                long size = strtol(arg, &arg, 10);
                if(size < 0 || size > 50)
                {
                    std::fprintf(stderr, "mkcromfs: Threads value may be 0..50. You gave %ld%s.\n", size,arg);
                    return -1;
                }
                if(size == 1) size = 0; // 1 thread is the same as no threads.
                UseThreads = size;
            #ifdef _OPENMP
                omp_set_num_threads(std::max(1u, UseThreads));
            #endif
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

    umask(0); // Prevent umask screwing up our permission bits.

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
