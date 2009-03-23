#define __STDC_CONSTANT_MACROS

#include "cromfs-defs.hh"
#include "threadfun.hh"
#include "assert++.hh"
#include "autodealloc.hh"

#include <vector>
#include <cstdio>
#include <cstdlib>
#include <map>
#include <set>
#include <algorithm>
#ifdef HAS_GCC_PARALLEL_ALGORITHMS
# include <parallel/algorithm>
#endif
#include <functional>

#include <unistd.h>
#include <dirent.h>
#include <fcntl.h>
#include <getopt.h>
#include <errno.h>
#include <sys/time.h>

#include <signal.h>

#include <sys/vfs.h> /* for statfs64 */

#ifdef _OPENMP
# include <omp.h>
#endif

#include "mkcromfs_sets.hh"
#include "sparsewrite.hh"

#include "lzma.hh"
#include "datasource.hh"
#include "datasource_detail.hh"
#include "cromfs-inodefun.hh"
#include "cromfs-directoryfun.hh"
#include "cromfs-blockifier.hh"
#include "longfileread.hh"
#include "longfilewrite.hh"
#include "nocopyarray.hh"
#include "util.hh"
#include "fnmatch.hh"
#include "mmap_vector.hh"

/* Settings */
#include "mkcromfs_sets.hh"
int LZMA_HeavyCompress = 1;
bool SortByFilename = true;
bool DecompressWhenLookup = false;
bool FollowSymlinks = false;
unsigned UseThreads = 0;
unsigned RandomCompressPeriod = 20000;
uint_fast32_t MinimumFreeSpace = 16;
uint_fast32_t AutoIndexPeriod = 256; // once every 256 bytes
uint_fast32_t MaxFblockCountForBruteForce = 2;
uint_fast32_t OverlapGranularity = 1;
bool MayPackBlocks = true;
bool MayAutochooseBlocknumSize = true;
bool MaySuggestDecompression = true;
std::string ReuseListFile;

BlockHashingMethods BlockHashing_Method = BlockHashing_All;


long FSIZE = 2097152;
long BSIZE = 65536;
std::vector<std::pair<std::string, long> > BSIZE_FOR;

//uint_fast32_t MaxSearchLength = FSIZE;


double RootDirInflateFactor = 1;
double InotabInflateFactor  = 1;
double BlktabInflateFactor  = 1;

static MatchingFileListType exclude_files;

/* Order in which to parse different types of directory entries */
static struct
{
    char Link, Dir, Other;
} DirParseOrder = { 2, 1, 3 };

/* Order in which to blockify different types of data */
static struct
{
    char Symlink, Directory, File, Inotab;
} DataClassOrder = { 2, 1, 3, 4 };

static bool MatchFile(const std::string& pathname)
{
    return !MatchFileFrom(pathname, exclude_files, false);
}

bool DisplayBlockSelections = true;
static bool DisplayFiles = true;
static bool DisplayEndProcess = true;

uint_fast32_t storage_opts = 0x00000000;

typedef std::pair<dev_t,ino_t> hardlinkdata;

static void FinalCompressFblock(cromfs_fblocknum_t fblocknum,
    mkcromfs_fblock& fblock,
    uint_fast64_t& compressed_total,
    uint_fast64_t& uncompressed_total)
{
    DataReadBuffer buf; uint_fast32_t fblock_rawlength;
    fblock.InitDataReadBuffer(buf, fblock_rawlength);

    char why[512];std::sprintf(why,"fblock %u", (unsigned)fblocknum);
    std::vector<unsigned char>
        fblock_lzma = DoLZMACompress(LZMA_HeavyCompress, buf.Buffer,fblock_rawlength, why);

    if(false)
    {
        bool is_ok = true;
        LZMADeCompress(fblock_lzma, is_ok);
        if(!is_ok)
        {
            std::fprintf(stderr,
                "Error: LZMA compression produced invalid LZMA data!\n"
                "       This should never happen. Sorry.\n");
        }
    }

    fblock.put_compressed(fblock_lzma);

    if(DisplayEndProcess)
    {
        std::printf(" [%d] %u -> %u\n",
            (int)fblocknum,
            (unsigned)fblock_rawlength,
            (unsigned)fblock_lzma.size());
        std::fflush(stdout);
    }

  #pragma omp atomic
    uncompressed_total += fblock_rawlength;
  #pragma omp atomic
    compressed_total   += fblock_lzma.size();
}

long CalcBSIZEfor(const std::string& pathfn)
{
    long result = BSIZE;

    for(std::vector<std::pair<std::string, long> >::const_iterator
        i = BSIZE_FOR.begin(); i != BSIZE_FOR.end(); ++i)
    {
        if(MatchFile(pathfn, i->first))
        {
            result = i->second;
        }
    }

    //fprintf(stderr, "bsize for %s = %ld\n", pathfn.c_str(), result);
    return result;
}

namespace cromfs_creator
{
    /*******************/
    /* Private methods */
    /*******************/

    struct DataSourceList
    {
        NoCopyArray<datasource_vector_ref> vector_refs;
        NoCopyArray<datasource_file_name>  filenames;
        NoCopyArray<datasource_vector>     vectors;
        NoCopyArray<datasource_symlink>    links;

        DataSourceList() : vector_refs(),filenames(),vectors(),links() { }

        void clear()
        {
            vector_refs.clear();
            filenames.clear();
            vectors.clear();
            links.clear();
        }
    } datasources;

    template<typename T1,typename T2>
    static inline datasource_t* NewVectorRefDatasource(const T1& a, const T2& b)
    {
        return datasources.vector_refs.push_construct(a, b);
    }

    template<typename T1,typename T2>
    static inline datasource_t* NewVectorDatasource(const T1& a, const T2& b)
    {
        return datasources.vectors.push_construct(a, b);
    }

    template<typename T1>
    static inline datasource_t* NewFilenameDatasource(const T1& a)
    {
        return datasources.filenames.push_construct(a);
    }

    template<typename T1,typename T2>
    static inline datasource_t* NewSymlinkDatasource(const T1& a, const T2& b)
    {
        return datasources.links.push_construct(a, b);
    }

    /***********************************/
    /* Filesystem traversal functions. *
     ***********************************/

    struct direntry
    {
        std::string pathname; // Source path
        std::string name;     // Just the entry name
        struct stat64 st;
        /*
        char sortkey;
        */

        uint_fast64_t      bytesize;   // Size in bytes (in case of a file or symlink)
        cromfs_inodenum_t* inonum;     // Where inode number will be written when known

        cromfs_dirinfo* dirinfo; // In case of a directory
        bool needs_blockify;

        direntry() : pathname(),name(), st()/*,sortkey()*/, // -Weffc++
            bytesize(0),inonum(0), dirinfo(0), needs_blockify()
        {
        }

        direntry(const direntry& b)
            : pathname(b.pathname), name(b.name),
              st(b.st), /*sortkey(b.sortkey),*/
              bytesize(b.bytesize),
              inonum(b.inonum), dirinfo(b.dirinfo),
              needs_blockify(b.needs_blockify) // -Weffc++
        {
        }

        /* The automatical operator=() would do just fine but
         * this implementation avoids a Valgrind warning from
         * memcpy(tgt,src,n) with tgt==src, invoked by mergesort.
         */
        direntry& operator=(const direntry& b)
        {
            if(&b != this)
            {
                pathname=b.pathname; name=b.name;
                st=b.st; /*sortkey=b.sortkey;*/
                bytesize=b.bytesize;
                inonum=b.inonum; dirinfo=b.dirinfo;
                needs_blockify=b.needs_blockify;
            }
            return *this;
        }

        bool CompareSortKey(const direntry& b) const
            { /*if(sortkey != b.sortkey) return sortkey < b.sortkey;*/
              //return *inonum < *b.inonum;
              if(SortByFilename) return name < b.name;
              /*return std::lexicographical_compare(
                name.rbegin(), name.rend(),
                b.name.rbegin(), b.name.rend());
              */
              return false;
            }
        bool CompareName(const direntry& b) const
            { return pathname < b.pathname;
            }
    };

#if defined(HAS_READDIR_R) && defined(_OPENMP) && (_OPENMP >= 200805)
 /*
  * The different recursion levels of WalkDir_Recursive()
  * can be multitasked, if the following conditions are met:
  *
  *  readdir_r is defined (readdir is not thread-safe)
  *  We have got OPENMP 3.0 (2.5 does not have "task")
  */

 #define USE_RECURSIVE_OMP_READDIR
#else
 #undef USE_RECURSIVE_OMP_READDIR
#endif

    class dircollection: public std::vector<direntry>
    {
    public:
        MutexType lock;

        dircollection(): std::vector<direntry>(),lock() {}
    };

    /* Reads the contents of one directory. Put rudimentary dircollection
     * entries into collection.
     */
    static void CollectOneDir(const std::string& path, dircollection& collection)
    {
        std::vector<std::string> dnames;

      {
        DIR* dir = opendir(path.c_str());
        if(!dir) { std::perror(path.c_str()); return; }

    #ifdef USE_RECURSIVE_OMP_READDIR
      {
        int dirent_len = offsetof(dirent, d_name) + pathconf(path.c_str(), _PC_NAME_MAX) + 1;
        std::vector<char> dirent_buf(dirent_len);
        dirent* dent = 0;
        while(readdir_r(dir, (dirent*)&dirent_buf[0], &dent)==0 && dent)
            dnames.push_back(dent->d_name);
      }
    #else
        while(dirent* dent = readdir(dir)) // Note: readdir is not re-entrant
            dnames.push_back(dent->d_name);
    #endif
        closedir(dir);
      } // scope for dir

        direntry ent;
        /* Fields common in all entries: */
        ent.inonum     = 0;
        ent.bytesize   = 0;
        ent.dirinfo    = 0;
        /* These fields will be set:
         * - pathname
         * - name
         * - st
         * - sortkey
         */
        /* And these won't just yet:
         * - needs_blockify
         */

        // using "long" here becase OpenMP 2.5 requires a signed iteration variable
        #pragma omp parallel for firstprivate(ent) schedule(guided)
        for(long a=0; a < (long)dnames.size(); ++a)
        {
            ent.name = dnames[a];
            if(ent.name == "." || ent.name == "..") continue;

            /*fprintf(stderr, "path(%s)name(%s)\n",
                path.c_str(),
                ent.name.c_str()); fflush(stderr);*/

            ent.pathname = path + "/" + ent.name;
            if(!MatchFile(ent.pathname)) continue;

            if( (FollowSymlinks ? stat64 : lstat64) (ent.pathname.c_str(), &ent.st) < 0)
            {
                std::perror(ent.pathname.c_str());
                continue;
            }
            /*
            if(S_ISLNK(ent.st.st_mode)) ent.sortkey = DirParseOrder.Link;
            else if(S_ISDIR(ent.st.st_mode)) ent.sortkey = DirParseOrder.Dir;
            else ent.sortkey = DirParseOrder.Other;
            */
            ScopedLock lck(collection.lock);
            collection.push_back(ent);
        }
    }

    /* Reads the contents of a subtree. It will fill in just
     * one field that CollectOneDir() did not do:
     * - bytesize
     *
     * It is done here because for directories, it cannot
     * be done before scanning the subdirectory contents.
     *
     * result_dirinfo is passed by reference instead of return
     * value, because the inonums are being assigned as pointers
     * to the parent's dirinfo.
     */
    static void WalkDir_Recursive
       (const std::string& path,
        // Collection and result_dirinfo are made pointers instead
        // of references so that they can be used in omp task.
        dircollection* const collection,
        cromfs_dirinfo* const result_dirinfo)
    {
        // Step 1: Scan the contents of this directory.
        //         Ignore entries which were not wanted (through MatchFile).

    #ifdef USE_RECURSIVE_OMP_READDIR
        dircollection tmpcollection;
        CollectOneDir(path, tmpcollection);

        collection->lock.Lock();
        const size_t collection_begin_pos = collection->size();
        collection->insert(collection->end(),
                          tmpcollection.begin(),
                          tmpcollection.end());
        const size_t collection_end_pos = collection->size();
        collection->lock.Unlock();
    #else
        const size_t collection_begin_pos = collection->size();
        CollectOneDir(path, *collection);
        const size_t collection_end_pos = collection->size();
    #endif
        // Step 2: Sort the contents of the directory according
        //         to the filename.
        // This must be done so that a directory gets the inode
        // numbers assigned in their name order.
        {
    #ifdef USE_RECURSIVE_OMP_READDIR
        ScopedLock lck(collection->lock);
    #endif
        MAYBE_PARALLEL_NS::sort(
            &(*collection)[collection_begin_pos],
            &(*collection)[collection_end_pos],
            std::mem_fun_ref(&direntry::CompareName));
        }

        // Step 3: Read subdirectories and calculate the number of
        //         blocks. Also assign the target for the inode number.
        //         (The target is in the cromfs_dirinfo of parent directory.)
        // Note: Directories are scanned _after_ the parent directory
        // is scanned, so that we will get each directory contiguously
        // in the array, and thus that the entries will have a contiguous
        // span of inode numbers.
        for(size_t p=collection_begin_pos; p<collection_end_pos; ++p)
        {
        #ifdef USE_RECURSIVE_OMP_READDIR
          #pragma omp task firstprivate(p,collection,result_dirinfo) shared(path)
          {
        #endif
            uint_fast64_t bytesize = 0;
            if(true) // scope
            {
                #ifdef USE_RECURSIVE_OMP_READDIR
                // We don't want our references to collection[]
                // to be moved to other location in RAM unexpectedly.
                ScopedLock lck(collection->lock);
                #endif

                /* Scan subdirectory, if one exists. */
                direntry& ent = (*collection)[p];
                const struct stat64& st = ent.st;
                bytesize = st.st_size; // Take file size
                if(S_ISDIR(st.st_mode))
                {
                    ent.dirinfo = new cromfs_dirinfo;
                    cromfs_dirinfo* const subdir = ent.dirinfo;

                    // Make a copy of the path name, because references
                    // to it from inside WalkDir_Recursive won't be valid after
                    // the collection gets reallocated.
                    std::string path_copy = ent.pathname;

                #ifdef USE_RECURSIVE_OMP_READDIR
                    lck.Unlock();
                #endif

                    // Note: If you use OpenMP 3.0 "task" here, you will
                    //       have to change readdir() into readdir_r().
                    WalkDir_Recursive(path_copy, collection, subdir);
                  /*
                    // If it was a directory, take the file size from
                    // the calculated cromfs directory size instead.
                    bytesize = calc_encoded_directory_size(subdir);
                  */
                }
            }
            // After the recursive call to WalkDir_Recursive(), the
            // reference to "ent" may have became invalid
            // due to vector reallocation, so we must take
            // a new reference and abandon the current one.
            if(true) // scope
            {
            #ifdef USE_RECURSIVE_OMP_READDIR
                ScopedLock lck(collection->lock);
            #endif

                direntry& ent = (*collection)[p];
                ent.bytesize   = bytesize;
                /* This inserts the entry in the directory
                 * listing, assigns it a dummy inode number (which
                 * will be filled later), and remembers the pointer
                 * to that inode number.
                 */
                ent.inonum = &((*result_dirinfo)[ent.name] = 0);
            }
        #ifdef USE_RECURSIVE_OMP_READDIR
          } // omp task
        #endif
        } // for collection
    }

    /* Scans some directory and schedules all the entries
     * found in it for blockifying. Returns the directory
     * contents as cromfs_dirinfo. Also updates the
     * bytes_of_files for statistics, and writes inodes
     * into inotab.
     */
    static
    cromfs_dirinfo WalkRootDir(
        const std::string& path,
        mmap_vector<unsigned char>& inotab,
        uint_fast64_t& bytes_of_files,
        cromfs_blockifier& blockifier
    )
    {
        typedef std::map<hardlinkdata, cromfs_inodenum_t> hardlinkmap_t;

        /*
            Step 1: Collect the directory tree, but don't
                    assign inode numbers or blocks.
                    Sort each directory by the name of entries.
                    The subdirectories come after their parent
                    directories, not in the midst thereof.

            Step 2: Assign inode numbers to entries of each
                    directory in succession. Do not go to
                    subdirectories before the parent is fully
                    inodified. At this step, also identify
                    hardlinked files. This ensures that each
                    directory has a contiguous sequence of
                    inode numbers, minimizing the fragmentation of
                    inotab access when the directory is readdir'd.

            Note:   Now each entry has an inode number.

            Step 3: Sort all entries by their DirParseOrder sort key.

            Step 4: Create the actual inodes for the files,
                    and schedule them for blockifying.

            Step 5: Patch the link count for hardlinked inodes.

            Step 6: Dispose of dynamically allocated data.
        */
        dircollection collection;
        cromfs_dirinfo root_dirinfo;
    #ifdef USE_RECURSIVE_OMP_READDIR
       #pragma omp parallel
       {
        #pragma omp single
        {
    #endif
        WalkDir_Recursive(path, &collection, &root_dirinfo); // Step 1.
    #ifdef USE_RECURSIVE_OMP_READDIR
       } // omp single
      } // omp parallel
    #endif

        uint_fast64_t inotab_size = inotab.size(); // Step 2.

        if(true) /* scope for hardlink_map */
        {
            hardlinkmap_t hardlink_map;
            // this is a non-threading context.
            for(size_t p=0; p<collection.size(); ++p)
            {
                direntry& ent = collection[p];
                const struct stat64& st = ent.st;

                const hardlinkdata hardlink(st.st_dev, st.st_ino);
                {hardlinkmap_t::const_iterator i = hardlink_map.find(hardlink);
                if(i != hardlink_map.end())
                {
                    /* A hardlink was found! */
                    const cromfs_inodenum_t inonum = i->second;

                    /* Reuse the same inode number. */
                    *ent.inonum = inonum;

                    ent.needs_blockify = false;
                    continue;
                }}

                // Create a new inode number and assign it for this entry.
                cromfs_inodenum_t inonum = get_first_free_inode_number(inotab_size);
                *ent.inonum = inonum;
                ent.needs_blockify = true;
                hardlink_map[hardlink] = inonum;

                // Although the inode was not yet really created, we will
                // simulate that it was, in order to be able to create new
                // inode numbers
                if(S_ISDIR(ent.st.st_mode))
                    ent.bytesize = calc_encoded_directory_size(*ent.dirinfo);
                uint_fast64_t num_blocks = CalcSizeInBlocks(ent.bytesize, CalcBSIZEfor(ent.pathname));

                inotab_size += INODE_SIZE_BYTES(num_blocks);
                // Ensure the inotab tail is 4-aligned.
                inotab_size = (inotab_size + 3UL) & ~3UL;
            }
        }

        if(inotab.size() < inotab_size)
            inotab.Resize(inotab_size);

        /* Now that the inode numbers have been assigned, we can
         * sort the entry list in the order in which we want to
         * blockify it.
         */
        /*
        MAYBE_PARALLEL_NS::stable_sort(
            collection.begin(),
            collection.end(),
            std::mem_fun_ref(&direntry::CompareSortKey)); // Step 3.
        */

        MutexType blockify_lock;

        #pragma omp parallel for schedule(guided)
        for(size_t p=0; p<collection.size(); ++p) // Step 4.
        {
            direntry& ent = collection[p];
            const struct stat64& st = ent.st;
            const std::string& pathname = ent.pathname;
            const cromfs_inodenum_t inonum = *ent.inonum;

            if(!ent.needs_blockify)
            {
                if(DisplayFiles)
                    std::printf("%s ... reused inode %ld (hardlink)\n", pathname.c_str(),
                        (long)inonum);
                continue;
            }

            /* Inode offset in inotab */
            const uint_fast32_t block_size = CalcBSIZEfor(ent.pathname);
            const uint_fast64_t inotab_offset = GetInodeOffset(inonum);
            #ifndef NDEBUG
            const uint_fast32_t num_blocks = CalcSizeInBlocks(ent.bytesize, block_size);
            #endif

            if(DisplayFiles)
            {
                std::printf(
                    "%s ... inode %ld. Size %llu"
                #ifndef NDEBUG
                    ", Spans from %p..%p"
                #endif
                    "\n",
                    pathname.c_str(),
                    (long)inonum,
                    (unsigned long long) ent.bytesize
                #ifndef NDEBUG
                    ,&inotab[inotab_offset],
                    &inotab[inotab_offset + INODE_SIZE_BYTES(num_blocks)]
                #endif
                );
                std::fflush(stdout);
            }


            /* Create inode */
            cromfs_inode_internal inode;
            inode.mode     = st.st_mode;
            inode.time     = st.st_mtime;
            //Link count starts from 1. Don't copy it from host filesystem.
            inode.links    = 1;
            inode.bytesize = 0;
            inode.rdev     = 0;
            inode.uid      = st.st_uid;
            inode.gid      = st.st_gid;
            inode.blocksize = block_size;

            if(S_ISDIR(st.st_mode))
            {
                cromfs_dirinfo& dirinfo = *ent.dirinfo;

                /* For directories, the number of links is
                 * the number of entries in the directory.
                 */
                inode.links = dirinfo.size();
                datasource_t* datasrc =
                    NewVectorDatasource(encode_directory(dirinfo), pathname);
                PutInodeSize(inode, datasrc->size());

                const uint_fast64_t headersize = INODE_HEADER_SIZE();

                ScopedLock lck(blockify_lock);
                blockifier.ScheduleBlockify(
                    datasrc,
                    DataClassOrder.Directory,
                    &inotab[inotab_offset + headersize],
                    inode.blocksize);
                // If you remove blockify_lock, make this atomic.
                bytes_of_files += inode.bytesize;
            }
            else if(S_ISLNK(st.st_mode))
            {
                datasource_t* datasrc =
                    NewSymlinkDatasource(pathname, st.st_size);
                PutInodeSize(inode, datasrc->size());

                const uint_fast64_t headersize = INODE_HEADER_SIZE();

                ScopedLock lck(blockify_lock);
                blockifier.ScheduleBlockify(
                    datasrc,
                    DataClassOrder.Symlink,
                    &inotab[inotab_offset + headersize],
                    inode.blocksize);
                // If you remove blockify_lock, make this atomic.
                bytes_of_files += inode.bytesize;
            }
            else if(S_ISREG(st.st_mode))
            {
                datasource_t* datasrc =
                    NewFilenameDatasource(pathname);
                PutInodeSize(inode, datasrc->size());

                const uint_fast64_t headersize = INODE_HEADER_SIZE();

                ScopedLock lck(blockify_lock);
                blockifier.ScheduleBlockify(
                    datasrc,
                    DataClassOrder.File,
                    &inotab[inotab_offset + headersize],
                    inode.blocksize);
                // If you remove blockify_lock, make this atomic.
                bytes_of_files += inode.bytesize;
            }
            else if(S_ISCHR(st.st_mode) || S_ISBLK(st.st_mode))
            {
                inode.rdev = st.st_rdev;
            }

            assertbegin();
            assert4var(num_blocks, inode.blocklist.size(), ent.bytesize, inode.bytesize);
            assert(num_blocks   == inode.blocklist.size());
            assert(ent.bytesize == inode.bytesize);
            assertflush();

            put_inode(&inotab[inotab_offset], inode, storage_opts);
        }

        // lastly, increment the linkcount for inodes that were hardlinked...
        for(size_t p=0; p<collection.size(); ++p) // Step 5.
        {
            direntry& ent = collection[p];
            if(!ent.needs_blockify)
            {
                uint_fast64_t pos = GetInodeOffset(*ent.inonum);
                unsigned char* inodata = &inotab[pos];
                increment_inode_linkcount(inodata);
            }
        }
        // The reason this loop is done separately from the previous
        // loop is because the previous loop accesses ent.inonum,
        // which may point into data that is being deallocated in
        // this loop.
        for(size_t p=0; p<collection.size(); ++p) // Step 6.
        {
            direntry& ent = collection[p];
            if(ent.dirinfo)
            {
                // it was allocated in WalkDir_Recursive(), but here's
                // the only place where it can be deallocated
                // nicely.
                delete ent.dirinfo;
            }
        }

        return root_dirinfo;
    }

    /***************************************/
    /* Start here: Walk through some path. */
    /***************************************/

    static int CreateAndWriteFs(
        const std::string& source_rootdir, int out_fd)
    {
        uint_fast64_t bytes_of_files = 0;

        // These two inodes will be written (compressed) into the filesystem.
        // They are the only inodes that won't be placed in inotab.
        std::vector<unsigned char> compressed_root_inode;
        std::vector<unsigned char> compressed_inotab_inode;
        std::vector<unsigned char> compressed_blktab;

        mmap_storage mmap_file(out_fd, 0);

        mmap_vector<cromfs_block_internal>* blocks = new mmap_vector<cromfs_block_internal> (mmap_file);
        autodealloc<mmap_vector<cromfs_block_internal> > blocks_dealloc(blocks);

        cromfs_blockifier blockifier(*blocks);

        if(true) // scope for inotab
        {
            // This array will collect all inodes of the filesystem. In the
            // end, it will be written as a file to the filesystem (split
            // into fblocks).
            mmap_vector<unsigned char> inotab(mmap_file);

            if(true) // scope for root_inode
            {
                const cromfs_dirinfo dirinfo
                    = WalkRootDir(
                        source_rootdir, inotab, bytes_of_files,
                        blockifier
                      );

                cromfs_inode_internal root_inode;
                root_inode.mode  = S_IFDIR | 0555;
                root_inode.time  = time(NULL);
                root_inode.links = dirinfo.size();
                root_inode.uid   = 0;
                root_inode.gid   = 0;
                root_inode.blocksize = BSIZE;

                datasource_t* datasrc =
                    NewVectorDatasource(encode_directory(dirinfo), "root dir");

                PutInodeSize(root_inode, datasrc->size());

                if(DisplayEndProcess)
                {
                    std::printf("Root pseudo file is %s\n",
                        ReportSize(root_inode.bytesize).c_str());
                }

                std::vector<unsigned char> raw_root_inode
                    = encode_inode(root_inode, storage_opts);

                const uint_fast64_t headersize = INODE_HEADER_SIZE();
                blockifier.ScheduleBlockify(
                    datasrc,
                    DataClassOrder.Directory,
                    &raw_root_inode[headersize],
                    root_inode.blocksize);

                if(DisplayFiles)
                {
                    std::printf("Paths scanned, now blockifying. Inotab size will be %s.\n",
                        ReportSize(inotab.size()).c_str());
                }

                // blockify all files&dirs, write block numbers in raw_root_inode.
                std::printf("Inotab spans %p..%p\n",
                    &inotab[0], &inotab[inotab.size()]);
                std::printf("Root inode spans %p..%p\n",
                    &raw_root_inode[headersize], &raw_root_inode[raw_root_inode.size()]);
                std::fflush(stdout);

                blockifier.FlushBlockifyRequests("Files and directories");

                std::fflush(stdout);
                if(DisplayEndProcess)
                    std::printf("Compressing raw rootdir inode (%s)\n",
                        ReportSize(raw_root_inode.size()).c_str());

                compressed_root_inode
                    = DoLZMACompress(LZMA_HeavyCompress, raw_root_inode, "raw_root_inode");

                if(DisplayEndProcess)
                    printf(" compressed into %s\n",
                        ReportSize(compressed_root_inode.size()).c_str());

            } // end scope for root_inode

            if(true) // scope for inotab_inode
            {
                cromfs_inode_internal inotab_inode;
                inotab_inode.mode = storage_opts;
                inotab_inode.time = time(NULL);
                inotab_inode.links = 1;
                inotab_inode.blocksize = CalcBSIZEfor("INOTAB");

                /* Before this line, all pending Blockify requests must be completed,
                 * because they will write data into inotab.
                 */

                datasource_t* datasrc =
                    NewVectorRefDatasource(inotab.GetAndRelease(), "INOTAB");

                PutInodeSize(inotab_inode, datasrc->size());

                if(DisplayEndProcess)
                {
                    std::printf("INOTAB pseudo file is %s\n",
                        ReportSize(inotab_inode.bytesize).c_str());
                }

                std::vector<unsigned char> raw_inotab_inode
                    = encode_inode(inotab_inode, storage_opts);

                const uint_fast64_t headersize = INODE_HEADER_SIZE();
                blockifier.ScheduleBlockify(
                    datasrc,
                    DataClassOrder.Inotab,
                    &raw_inotab_inode[headersize],
                    inotab_inode.blocksize);

                // blockify inotab, write block numbers in raw_inotab_inode.
                std::printf("Inotab inode spans %p..%p\n",
                    &raw_inotab_inode[headersize], &raw_inotab_inode[raw_inotab_inode.size()]);
                std::fflush(stdout);

                blockifier.FlushBlockifyRequests("INOTAB");

                std::fflush(stdout);

                blockifier.EnablePackedBlocksIfPossible();

                // Poke in the storage opts again, because storage_opts may have
                // been changed since the last write due to EnablePackedBlocks.
                W32(&raw_inotab_inode[0], storage_opts);

                std::fflush(stdout);

                if(DisplayEndProcess)
                    std::printf("Compressing raw inotab inode (%s)\n",
                        ReportSize(raw_inotab_inode.size()).c_str());

                compressed_inotab_inode
                    = DoLZMACompress(LZMA_HeavyCompress, raw_inotab_inode, "raw_inotab_inode");

                if(DisplayEndProcess)
                    std::printf(" compressed into %s\n",
                        ReportSize(compressed_inotab_inode.size()).c_str());

            } // end scope for inotab_inode
        } // end scope for inotab

        mkcromfs_fblockset& fblocks                = blockifier.fblocks;

        if(true) // scope for raw_blktab
        {
            // Data locators written into filesystem. Indexed by block number.

            unsigned onesize = DATALOCATOR_SIZE_BYTES();
            if(DisplayEndProcess)
            {
                std::fflush(stdout);
                std::printf("Compressing %u block records (%u bytes each, total %s)\n",
                    (unsigned)blocks->size(), onesize, ReportSize(blocks->size() * onesize).c_str());
                std::fflush(stdout);
            }

            std::vector<unsigned char> raw_blktab(blocks->size() * onesize);
            if(storage_opts & CROMFS_OPT_PACKED_BLOCKS)
                for(unsigned a=0; a<blocks->size(); ++a)
                {
                    uint_fast32_t fblocknum = (*blocks)[a].get_fblocknum(BSIZE,FSIZE);
                    uint_fast32_t startoffs = (*blocks)[a].get_startoffs(BSIZE,FSIZE);

                    //fprintf(stderr, "Writing P block %u = %u:%u\n", a,fblocknum,startoffs);

                    W32(&raw_blktab[a*onesize], fblocknum * FSIZE + startoffs);
                }
            else
                for(unsigned a=0; a<blocks->size(); ++a)
                {
                    uint_fast32_t fblocknum = (*blocks)[a].get_fblocknum(BSIZE,FSIZE);
                    uint_fast32_t startoffs = (*blocks)[a].get_startoffs(BSIZE,FSIZE);

                    //fprintf(stderr, "Writing NP block %u = %u:%u\n", a,fblocknum,startoffs);

                    W32(&raw_blktab[a*onesize+0], fblocknum);
                    W32(&raw_blktab[a*onesize+4], startoffs);
                }

            if(LZMA_HeavyCompress==2)
            {
                compressed_blktab = LZMACompressHeavy(raw_blktab, "raw_blktab");
            }
            else
            {
                /* Make an educated guess of the optimal parameters for blocktab compression */
                const unsigned blktab_periodicity
                    = (DATALOCATOR_SIZE_BYTES() == 4) ? 2 : 3;

                compressed_blktab = LZMACompress(raw_blktab,
                    blktab_periodicity,
                    blktab_periodicity,
                    0);
            }

            if(DisplayEndProcess)
            {
                std::printf(" compressed into %s\n", ReportSize(compressed_blktab.size()).c_str()); std::fflush(stdout);
            }

            delete blocks;
            blocks = 0;
        } // end scope for raw_blktab and blocks

        blockifier.NoMoreBlockifying();
        datasources.clear();

        cromfs_superblock_internal sblock;
        sblock.sig          = CROMFS_SIGNATURE;
        sblock.rootdir_size = compressed_root_inode.size();
        sblock.inotab_size  = compressed_inotab_inode.size();
        sblock.blktab_size  = compressed_blktab.size();

        sblock.rootdir_room = uint_fast64_t(sblock.rootdir_size * RootDirInflateFactor);
        sblock.inotab_room  = uint_fast64_t(sblock.inotab_size  * InotabInflateFactor);
        sblock.blktab_room  = uint_fast64_t(sblock.blktab_size  * BlktabInflateFactor);

        sblock.fsize          = FSIZE;
        sblock.bsize          = BSIZE;
        sblock.bytes_of_files = bytes_of_files;

        sblock.SetOffsets();

        cromfs_superblock_internal::BufferType Superblock;
        sblock.WriteToBuffer(Superblock);

        ftruncate64(out_fd, 0);

      #pragma omp parallel sections
      {
        #pragma omp section
        {
        SparseWrite(out_fd, Superblock, sblock.GetSize(), 0); }
        #pragma omp section
        { //fprintf(stderr, "root goes at %llX\n", sblock.rootdir_offs);
        SparseWrite(out_fd, &compressed_root_inode[0],   compressed_root_inode.size(), sblock.rootdir_offs); }
        #pragma omp section
        { //fprintf(stderr, "inotab goes at %llX\n", sblock.inotab_offs);
        SparseWrite(out_fd, &compressed_inotab_inode[0], compressed_inotab_inode.size(), sblock.inotab_offs); }
        #pragma omp section
        { SparseWrite(out_fd, &compressed_blktab[0], compressed_blktab.size(), sblock.blktab_offs); }
      }
        if(DisplayEndProcess)
        {
            std::printf("Compressing and writing %u fblocks...\n",
                (unsigned)fblocks.size());
            std::fflush(stdout);
        }

        uint_fast64_t compressed_total = 0;
        uint_fast64_t uncompressed_total = 0;

        bool terminate_for = false;

      #ifdef _OPENMP
        int backup_max_threads = omp_get_max_threads(),
            backup_nested = omp_get_nested();
        if(LZMA_HeavyCompress)
        {
            // For cache performance it is better to multithread
            // the LZMA optimizer than this outer loop. Therefore,
            // we set the number of threads to 1 for this loop,
            // and restore it immediately after the team is created.
            // The nesting flag is enabled in order to allow the
            // nested parallel setting to have multiple threads.
            // However, we do this only for LZMA_HeavyCompress
            // because the non-heavy version does not do threading.
            omp_set_num_threads(1);
            omp_set_nested(1);
        }
      #endif

        uint_fast64_t fblk_offset = sblock.fblktab_offs;

        /* Note: Using "long" for loop iteration variable, because OpenMP
         * requires the loop iteration variable to be of _signed_ type,
         * and cromfs_fblocknum_t is unsigned.
         */

      #pragma omp parallel for ordered schedule(dynamic) \
            reduction(+:compressed_total) \
            reduction(+:uncompressed_total)
        for(long/*cromfs_fblocknum_t*/ fblocknum=0; fblocknum<(long)fblocks.size(); ++fblocknum)
        {
          #ifdef _OPENMP
            omp_set_num_threads(backup_max_threads);
          #endif

            mkcromfs_fblock& fblock = fblocks[fblocknum];

          #pragma omp flush(terminate_for)
            if(!terminate_for)
                FinalCompressFblock(fblocknum, fblock, compressed_total, uncompressed_total);

          #pragma omp ordered
          {
            DataReadBuffer lzma_buffer;
            uint_fast32_t  lzma_length;

            fblock.InitCompressedDataReadBuffer(lzma_buffer, lzma_length);

            if(true)
            {
                bool is_ok = true;
                LZMADeCompress(lzma_buffer.Buffer, lzma_length, is_ok);
                if(!is_ok)
                {
                    std::fprintf(stderr,
                        "Error: Compressed fblock %ld ended up being invalid LZMA data!\n"
                        "       This should never happen. Sorry.\n",
                        fblocknum);
                    terminate_for = true;
                  #pragma omp flush(terminate_for)
                    goto next_for;
                }
            }

            { unsigned char Buf[64];
              W32(Buf, lzma_length);
              SparseWrite(out_fd, Buf, 4, fblk_offset);
              fblk_offset += 4;
            }

            SparseWrite(out_fd, lzma_buffer.Buffer, lzma_length, fblk_offset);

            if(storage_opts & CROMFS_OPT_SPARSE_FBLOCKS)
            {
                if(lzma_length > (size_t)FSIZE)
                {
                    std::printf("\n");
                    std::fflush(stdout);
                    std::fprintf(stderr,
                        "Error: This filesystem cannot be sparse, because a compressed fblock\n"
                        "       actually became larger than the decompressed one.\n"
                        "       Sorry. Try the 'minfreespace' option if it helps.\n"
                     );
                    terminate_for = true;
                  #pragma omp flush(terminate_for)
                    goto next_for;
                }

                fblk_offset += FSIZE;
            }
            else
            {
                fblk_offset += lzma_length;
            }

            // Ensure the data is written before deleting the fblock
            // So that if the computer happens to crash, a recovery
            // with --finish-interrupted is possible.

            fdatasync(out_fd);
            fblock.Unmap();
            fblock.Close();
            //fblock.Delete();
            std::printf("You may now delete %s if you want\n", fblock.getfn().c_str());

            std::fflush(stdout);
          next_for:;
          }
        }
      #ifdef _OPENMP
        omp_set_num_threads(backup_max_threads);
        omp_set_nested(backup_nested);
      #endif

        if(terminate_for) return -1;

        ftruncate64(out_fd, fblk_offset);

        if(DisplayEndProcess)
        {
            uint_fast64_t file_size = fblk_offset;

            std::printf(
                "\n%u fblocks were written: %s = %.2f %% of %s\n",
                (unsigned)fblocks.size(),
                ReportSize(compressed_total).c_str(),
                compressed_total * 100.0 / (double)uncompressed_total,
                ReportSize(uncompressed_total).c_str()
               );
            std::printf(
                "Filesystem size: %s = %.2f %% of original %s\n",
                ReportSize(file_size).c_str(),
                file_size * 100.0 / (double)bytes_of_files,
                ReportSize(bytes_of_files).c_str()
                   );
        }

        return 0;
    }

    static int ResumeWritingFs(
        const std::string& fblock_pattern, int out_fd)
    {
        set_fblock_name_pattern(fblock_pattern);

        cromfs_superblock_internal sblock;

        cromfs_superblock_internal::BufferType Superblock;
        ( LongFileRead(out_fd, 0, sizeof(Superblock), Superblock) );

        //storage_opts = 0;
        sblock.ReadFromBuffer(Superblock);

        uint_fast64_t fblock_pos = sblock.fblktab_offs;

        //const size_t max_num_fblocks = (size_t)-1;

        /* Note: This loop cannot be made parallel, because each loop
         * iteration requires knowing the value of fblock_pos, and that
         * value is updated on the previous loop.
         * Not even the "ordered" OpenMP clause can help this.
         */
        bool error = false;
        for(size_t fblocknum=0; /*fblocknum<max_num_fblocks*/; ++fblocknum)
        {
            std::printf("checking the state of fblock %u... (supposedly at file position 0x%X)\n",
                (int)fblocknum, (unsigned)fblock_pos);
            try
            {
                unsigned char SizeBuffer[4];
                LongFileRead(out_fd, fblock_pos, 4, SizeBuffer);

                uint_fast64_t fblock_lzma_size = R32(SizeBuffer);

                std::vector<unsigned char> fblockdata(fblock_lzma_size);

                LongFileRead(out_fd, fblock_pos+4, fblock_lzma_size, &fblockdata[0]);

                std::printf(" fblock %u appears to be in the file already (%lu bytes)\n",
                    (int)fblocknum, (unsigned long)fblockdata.size());

                bool is_ok = true;

                /*if(fblocknum >= 190)*/ LZMADeCompress(fblockdata, is_ok);
                if(!is_ok)
                {
                    std::printf(" but it is not valid LZMA data!\n");
                    error = true;
                    throw EINVAL;
                }

                fblock_pos += fblock_lzma_size + 4;
                continue;
            }
            catch(int) // fblock was not found in the target file
            {
                std::printf(" checking the fblock file for fblock %u...\n", (int)fblocknum);

                mkcromfs_fblock fblock(fblocknum);

                if(fblock.getfilesize() == 0)
                {
                    std::printf(" file for fblock %u does not seem to exist\n", (int)fblocknum);
                    break;
                }

                if(fblock.is_uncompressed())
                {
                    std::printf(" it existed, compressing...\n");
                    uint_fast64_t com=0, uncom=0;
                    FinalCompressFblock(fblocknum, fblock, com, uncom);
                }

                DataReadBuffer lzma_buffer;
                uint_fast32_t  lzma_length;

                fblock.InitCompressedDataReadBuffer(lzma_buffer, lzma_length);

                std::printf(" writing fblock %u (%u bytes) at 0x%lX\n",
                    (int)fblocknum, (unsigned) lzma_length,
                    (unsigned long)fblock_pos);

                { unsigned char SizeBuffer[64];
                  W32(SizeBuffer, lzma_length);
                  LongFileWrite(out_fd, fblock_pos, 4, SizeBuffer); }

                SparseWrite(out_fd, lzma_buffer.Buffer, lzma_length, fblock_pos+4);

                if(storage_opts & CROMFS_OPT_SPARSE_FBLOCKS)
                {
                    if(lzma_length > (size_t)FSIZE)
                    {
                        std::printf("\n");
                        std::fflush(stdout);
                        std::fprintf(stderr,
                            "Error: This filesystem cannot be sparse, because a compressed fblock\n"
                            "       actually became larger than the decompressed one.\n"
                            "       Sorry. Try the 'minfreespace' option if it helps.\n"
                         );
                        return 1;
                    }

                    fblock_pos += 4 + FSIZE;
                }
                else
                {
                    fblock_pos += 4 + lzma_length;
                }

                std::fflush(stdout);
            }
        }
        std::printf("Done\n");
        if(!error) ftruncate64(out_fd, fblock_pos);

        return 0;
    }
}

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

class EstimateSpaceNeededFor
{
private:
    MutexType hardlink_lock;
    std::set<hardlinkdata> hardlink_set;

    bool check_hardlink_file(dev_t dev, ino_t ino)
    {
        hardlinkdata d(dev, ino);

        ScopedLock lck(hardlink_lock);
        std::set<hardlinkdata>::const_iterator i = hardlink_set.find(d);
        if(i == hardlink_set.end()) { hardlink_set.insert(d); return false; }
        return true;
    }
public:
    uint_fast64_t num_blocks;
    uint_fast64_t num_file_bytes;
    uint_fast64_t num_inodes;

    EstimateSpaceNeededFor(const std::string& path):
        hardlink_lock(), hardlink_set(), num_blocks(0),
        num_file_bytes(0), num_inodes(0)
    {
        Handle(path);
    }
private:
    void Handle(const std::string& path)
    {
        DIR* dir = opendir(path.c_str());
        if(!dir) return;

        std::vector<std::string> dnames;
        while(dirent* dent = readdir(dir)) // Note: readdir is not re-entrant
            dnames.push_back(dent->d_name);
        closedir(dir);

        std::vector<std::string> dirs;

        MutexType DirsLock;

        // using "long" here becase OpenMP 2.5 requires a signed iteration variable
        uint_fast64_t n_filebytes=0, n_blocks=0, n_ino=0;
        #pragma omp parallel for schedule(guided) reduction(+:n_filebytes,n_blocks,n_ino)
        for(long a=0; a < (long)dnames.size(); ++a)
        {
            const std::string entname = dnames[a];
            if(entname == "." || entname == "..") continue;
            const std::string pathname = path + "/" + entname;
            if(!MatchFile(pathname)) continue;
            struct stat64 st;
            if( (FollowSymlinks ? stat64 : lstat64) (pathname.c_str(), &st) < 0) continue;

            /* Count the size of the directory entry */
            num_file_bytes += 8 + entname.size() + 1;

            if(check_hardlink_file(st.st_dev, st.st_ino))
            {
                continue; // nothing more to do for this entry
            }

            if(S_ISDIR(st.st_mode))
            {
                ScopedLock lck(DirsLock);
                dirs.push_back(pathname);
                continue;
            }

            /* Count the size of the content */
            uint_fast64_t file_size   = st.st_size;
            uint_fast64_t file_blocks = CalcSizeInBlocks(file_size, CalcBSIZEfor(pathname));

            // No #pragma omp atomic required here,
            // already covered by the "reduction" clause
            n_filebytes += file_size;
            n_blocks    += file_blocks;
            n_ino       += 1;
        }
        num_file_bytes += n_filebytes;
        num_blocks     += n_blocks;
        num_inodes     += n_ino;

        for(unsigned a=0; a<dirs.size(); ++a)
        {
            // Note: If you use OpenMP 3.0 "task" here, you will
            //       have to change readdir() into readdir_r().
            Handle(dirs[a]);
        }
    }
};

class CheckSomeDefaultOptions
{
public:
    CheckSomeDefaultOptions(const std::string& rootpath)
    {
        EstimateSpaceNeededFor estimate(rootpath);

        if(MayAutochooseBlocknumSize)
        {
            if(estimate.num_blocks < 0x10000UL)
            {
                std::printf(
                    "mkcromfs: Automatically enabling --16bitblocknums because it seems possible for this filesystem.\n");
                storage_opts |= CROMFS_OPT_16BIT_BLOCKNUMS;
            }
            else if(estimate.num_blocks < 0x1000000UL)
            {
                std::printf(
                    "mkcromfs: Automatically enabling --24bitblocknums because it seems possible for this filesystem.\n");
                storage_opts |= CROMFS_OPT_24BIT_BLOCKNUMS;
            }
        }

        if(MaySuggestDecompression)
        {
            uint_fast64_t space_needed =
                estimate.num_inodes * INODE_HEADER_SIZE()
              + estimate.num_blocks * BLOCKNUM_SIZE_BYTES()
              + estimate.num_blocks * DATALOCATOR_SIZE_BYTES()
              + estimate.num_file_bytes;

            std::string TempDir = GetTempDir();
            struct statfs64 stats;
            if(statfs64(TempDir.c_str(), &stats) != 0)
            {
                perror(TempDir.c_str()); // not a severe error though
            }
            else
            {
                uint_fast64_t space_have   = stats.f_bfree * stats.f_bsize;

                if(space_needed >= space_have) goto NotEnabled;

                std::printf(
                    "Good news! Your tempdir, [1m%s[0m, has [1m%s[0m free.\n"
                    "mkcromfs estimates that if the [1m-e -r100000[0m options were enabled,\n"
                    "it would require about [1m%s[0m of temporary disk space.\n"
                    "This would be likely much faster than the default method.\n"
                    "The default method requires less temporary space, but is slower.\n",
                    TempDir.c_str(),
                    ReportSize(space_have).c_str(),
                    ReportSize(space_needed).c_str());

                if(space_needed < space_have / 20)
                {
                    std::printf(
                        "Actually, the ratio is so small that I'll just enable\n"
                        "the mode without even asking. You can suppress this behavior\n"
                        "by manually specifying one of the -e, -r or -q options.\n");
                    goto Enabled;
                }
                for(;;)
                {
                    std::printf(
                        "Do you want to enable the 'decompresslookups' mode? (Y/N) ");
                    std::fflush(stdout);
                    char Buf[512];
                    if(!std::fgets(Buf, sizeof(Buf), stdin))
                    {
                        std::printf("\nEOF from stdin, assuming the answer is \"no\".\n");
                        goto NotEnabled;
                    }
                    if(Buf[0] == 'y' || Buf[0] == 'Y') goto Enabled;
                    if(Buf[0] == 'n' || Buf[0] == 'N') goto NotEnabled;
                    std::printf("Invalid input, please try again...\n");
                }
            Enabled:
                std::printf("Activating commandline options: -e -r100000%s\n",
                    MaxFblockCountForBruteForce ? "" : " -c2");
                DecompressWhenLookup = true;
                RandomCompressPeriod = 100000;
                if(!MaxFblockCountForBruteForce) MaxFblockCountForBruteForce = 2;
            NotEnabled: ;
            }
        }
    }
};

const char* GetTempDir()
{
    const char* t;
    t = std::getenv("TEMP"); if(t) return t;
    t = std::getenv("TMP"); if(t) return t;
    return "/tmp";
}

static void CleanupTempsExit(int signo)
{
    std::printf("\nTermination signalled, cleaning up temporaries\n");
    signal(signo, SIG_DFL);
    raise(signo); // reraise the signal
}

static void set_default_fblock_name_pattern()
{
    const std::string tmpdir = GetTempDir();
    const int pid = getpid();
    char Buf[4096];
    std::sprintf(Buf, "/fblock_%d-", pid);
    set_fblock_name_pattern(tmpdir + Buf);
}

int main(int argc, char** argv)
{
    std::string path  = ".";
    std::string outfn = "cromfs.bin";

    std::string resume_file_selection = "";

    long AutoIndexRatio = 0;

    signal(SIGINT, CleanupTempsExit);
    signal(SIGTERM, CleanupTempsExit);
    signal(SIGHUP, CleanupTempsExit);
    signal(SIGQUIT, CleanupTempsExit);

    for(;;)
    {
        int option_index = 0;
        static struct option long_options[] =
        {
            {"help",        0, 0,'h'},
            {"version",     0, 0,'V'},
            {"fsize",       1, 0,'f'},
            {"bsize",       1, 0,'b'},
            {"bsize_for",   1, 0,'B'},
            {"decompresslookups",
                            0, 0,'e'},
            {"randomcompressperiod",
                            1, 0,'r'},
            {"minfreespace",
                            1, 0,'s'},
            {"autoindexratio",
                            1, 0,'a'},
            {"autoindexperiod",
                            1, 0,'A'},
            {"bruteforcelimit",
                            1, 0,'c'},
            {"followsymlinks", 0,0,'l'},
            {"exclude",     1, 0,'x'},
            {"exclude-from",1, 0,'X'},
            {"quiet",       0, 0,'q'},
            {"verbose",     0, 0,'v'},
            {"sparse",      1, 0,'S'},
            {"blockifybufferlength",    1,0,3001},
            {"blockifybufferincrement", 1,0,3002},
            {"blockifyoptimizemethod",  1,0,3003},
            {"blockifyoptimisemethod",  1,0,3003},
            {"blockindexmethod",        1,0,3004},
            {"32bitblocknums",          0,0,'4'},
            {"24bitblocknums",          0,0,'3'},
            {"16bitblocknums",          0,0,'2'},
            {"nopackedblocks",          0,0,6001},
            {"lzmafastbytes",           1,0,4001},
            {"lzmabits",                1,0,4002},
            {"threads",                 1,0,4003},
            {"blockifyorder",           1,0,5001},
            {"dirparseorder",           1,0,5002},
            {"nosortbyfilename",        0,0,5003},
            {"bwt",                     0,0,2001},
            {"mtf",                     0,0,2002},
            {"overlapgranularity",      1,0,'g'},

            {"finish-interrupted",      1,0,7001},
            {"resume-blockify",         1,0,7002},
            {0,0,0,0}
        };
        int c = getopt_long(argc, argv, "hVvf:b:B:er:s:a:A:c:qx:X:lS:432g:", long_options, &option_index);
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
                    "mkcromfs v"VERSION" - Copyright (C) 1992,2009 Bisqwit (http://iki.fi/bisqwit/)\n"
                    "\n"
                    "Usage: mkcromfs [<options>] <input_path> <target_image>\n"
                    "\n"
                    "Basic options:\n"
                    " --help, -h         This help\n"
                    " --version, -V      Displays version information\n"
                    " --quiet, -q\n"
                    "     -q supresses the detailed information outputting while compressing.\n"
                    "     -qq supresses also the listing of the filenames.\n"
                    "     -qqq supresses also the summary and progress at the last phase.\n"
                    " --verbose, -v\n   Opposite meaning of --quiet, but not exactly\n"
                    "     -v makes auto/full LZMA compression a bit more verbose\n"
                    "     -vv makes auto/full LZMA compression a lot more verbose\n"
                    "\n"
                    "File selection parameters:\n"
                    " --exclude, -x <pattern>\n"
                    "     Exclude files matching <pattern> from the archive\n"
                    " --exclude-from, -X <file>\n"
                    "     Exclude files matching the patterns in <file>\n"
                    " --followsymlinks, -l\n"
                    "     Follow symlinks instead of storing them (same the referred contents)\n"
                    " Note: The pathname seen by the exclude\n"
                    "       matchers includes the source path.\n"
                    "\n"
                    "Operation parameters:\n"
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
                    " --threads <value>\n"
                    "     Use the given number of threads.\n"
                    "     The threads will be used in the substring finder and the fblock compressor.\n"
                    "     This option only makes sense if the value is smaller\n"
                    "     or equal to the value of --bruteforcelimit.\n"
                    "     Use 0 or 1 to disable threads. (Default)\n"
                    " --finish-interrupted <fblock_path_prefix>\n"
                    "     Use this option to finish a cromfs filesystem, if for some\n"
                    "     reason mkcromfs was interrupted during the final phase of\n"
                    "     LZMA-compressing fblocks (due to blackout for example).\n"
                    "     Example:\n"
                    "       mkcromfs --finish-interrupted '/path/to/fblock_6207-' out.cromfs files...\n"
                    " --resume-blockify <resumefile_prefix>\n"
                    "     Use this option if you want the possibility to save the\n"
                    "     progress after the \"finding identical blocks\" phase\n"
                    "     or to load a previously saved progress.\n"
                    "     Example:\n"
                    "       mkcromfs --resume-blockify '/path/to/resume-' out.cromfs files...\n"
                    "\n"
                    "Filesystem parameters:\n"
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
                    " --bsize_for, -B <pattern>:<size>\n"
                    "     Overrides the default bsize value for files matching <pattern>.\n"
                    "     This option increases the sizes of inodes with 4 bytes, but\n"
                    "     allows each file to have a different block size. You can specify\n"
                    "     this option multiple times for each different pattern. Files that\n"
                    "     do not match any pattern use the default bsize (--bsize)\n"
                    "     Note: The pathname seen by the pattern\n"
                    "           matcher includes the source path.\n"
#if 0
                    " --sparse, -S <opts>\n"
                    "     Commaseparated list of items to store sparsely. -Sf = fblocks\n"
                    "     Use this option only when creating appendable filesystems.\n"
#endif
                    " --24bitblocknums, -3\n"
                    "     Tells mkcromfs to store block numbers in 3 bytes instead of 4.\n"
                    "     This will result in smaller inodes, potentially saving space\n"
                    "     if your filesystem contains a lot of files. However, it will\n"
                    "     also restrict the number of blocks to 16777216 instead of the\n"
                    "     default value of 4294967296.\n"
                    "     If your total size of files divided by the selected bsize\n"
                    "     approaches this limit, do not use this option.\n"
                    " --16bitblocknums, -2\n"
                    "     Tells mkcromfs to store block numbers in 2 bytes instead of 4.\n"
                    "     This will result in smaller inodes, potentially saving space\n"
                    "     if your filesystem contains a lot of files. However, it will\n"
                    "     also restrict the number of blocks to 65536 instead of the\n"
                    "     default value of 4294967296.\n"
                    "     If your total size of files divided by the selected bsize\n"
                    "     approaches this limit, do not use this option.\n"
                    " --32bitblocknums, -4\n"
                    "     By default, mkcromfs automatically chooses --16bitblocknums\n"
                    "     or --24bitblocknums if it detects that either one is possible.\n"
                    "     However, with this option you can force it to use 32-bit blocknums\n"
                    "     instead, overriding the automatical detection.\n"
                    " --nopackedblocks\n"
                    "     By default, mkcromfs stores blocks in 4 bytes instead of 8 bytes,\n"
                    "     if it is possible for the filesystem being created. However, you\n"
                    "     may use this option to disable it (for example, if you are creating\n"
                    "     a filesystem that may be write-extended).\n"
                    "     This option supersedes the old --packedblocks (-k) with opposite\n"
                    "     semantics.\n"
                    "\n"
                    "Compression algorithm parameters:\n"
                    " --minfreespace, -s <value>\n"
                    "     Minimum free space in a fblock to consider it a candidate. Default: 16\n"
                    "     The value should be smaller than bsize, or otherwise it works against\n"
                    "     the fsize setting by making the fsize impossible to reach.\n"
                    "     Note: The bruteforcelimit algorithm ignores minfreespace.\n"
                    " --autoindexperiod, -A <value>\n"
                    "     Controls how often a new automatic index will be added in an fblock.\n"
                    "     For example, value 32 tells that a new index will be added every 32 bytes.\n"
                    "     Value of 0 disables autoindex alltogether.\n"
                    "     (Overlaps still cause predictive autoindexing to happen.)\n"
                    "     Default: 256\n"
                    " --autoindexratio, -a <value>\n"
                    "     Deprecated option.\n"
                    "     Equivalent to --autoindexperiod <bsize / value>\n"
                    " --bruteforcelimit, -c <value>\n"
                    "     Set the maximum number of previous fblocks to search for\n"
                    "     overlapping content when deciding which fblock to append to.\n"
                    "     Value 0 means doing straight-forward selection based on the free\n"
                    "     space in the fblock. Default: 2\n"
                    " --lzmafastbytes <value>\n"
                    "     Specifies the number of \"fast bytes\" in LZMA compression\n"
                    "     algorithm. Valid values are 5..273. Default is 273.\n"
                    " --lzmabits <pb>,<lp>,<lc>\n"
                    "     Sets the values for PosStateBits, LiteralPosStateBits\n"
                    "     and LiteralContextBits in LZMA properties.\n"
                    "      pb: Default value 0, allowed values 0..4\n"
                    "      lp: Default value 0, allowed values 0..4\n"
                    "      lc: Default value 1, allowed values 0..8\n"
                    "     Further documentation on these values is available in LZMA SDK.\n"
                    "     See file util/lzma/lzma.txt in the cromfs source distribution.\n"
                    "     Example: --lzmabits 0,0,3 (for text data)\n"
                    "     Alternatively, you can choose \"--lzmabits full\", which will\n"
                    "     try every possible option. Beware it will consume lots of time.\n"
                    "     \"--lzmabits auto\" is a lighter alternative to \"--lzmabits full\",\n"
                    "     and enabled by default.\n"
                    " --blockifyorder <value>\n"
                    "     Specifies the priorities for blockifying different types of data\n"
                    "     Default: dir=1,link=2,file=3,inotab=4\n"
                    "     Changing it may affect compressibility.\n"
                    " --blockindexmethod <value>\n"
                    "     Controls the way how identical blocks are recognized.\n"
                    "     Four possible methods exist:\n"
                    "       --blockindexmethod none\n"
                    "            Blocks are not indexed. Fastest, uses least memory,\n"
                    "            but also neglects most of cromfs's power.\n"
                    "       --blockindexmethod all (default)\n"
                    "            All blocks are indexed before blockifying.\n"
                    "            If your filesystem is large (millions of blocks),\n"
                    "            note that the index construction may require\n"
                    "            excessive amounts of virtual memory.\n"
                    "       --blockindexmethod all2\n"
                    "            A version of \"all\" that uses an extra 512 MiB\n"
                    "            of virtual memory to avoid testing unique hashes.\n"
                    "       --blockindexmethod prepass\n"
                    "            This option uses an extra 1024 MiB of virtual memory\n"
                    "            before running the \"all\" blocks indexing in the\n"
                    "            hopes of consuming less virtual memory overall.\n"
                    "            Use it if \"all2\" consumes way too much virtual memory.\n"
                    "       --blockindexmethod collect\n"
                    "            All blocks are hashed before blockifying.\n"
                    "            This requires a predictable amount of memory\n"
                    "            (4 * block count bytes), but is considerably\n"
                    "            slower than the \"all\" option.\n"
                    "       --blockindexmethod collect2\n"
                    "            A version of \"collect\" that uses an extra 512 MiB\n"
                    "            of virtual memory to avoid testing unique hashes.\n"
                    "       --blockindexmethod blanks\n"
                    "            Only zero-filled blocks are indexed.\n"
                    "            This parallels the \"sparse file\" optimization\n"
                    "            that occurs in most compressing filesystems,\n"
                    "            and requires relatively little virtual memory. It\n"
                    "            neglects most of cromfs's power, though.\n"
                    "            Use it if \"all2\" consumes too much virtual memory.\n"
                    " --nosortbyfilename\n"
                    "     Disables sorting by filename when blockifying. Use when\n"
                    "     you have made attempts to affect manually the order in which\n"
                    "     files are blockified.\n"
                    " --dirparseorder <value>\n"
                    "     Specifies the priorities for storing different types of elements\n"
                    "     in directories. Default:  dir=1,link=2,other=3\n"
                    "     Changing it may affect compressibility.\n"
                    " --overlapgranularity, -g <value>\n"
                    "     Forbids overlap lengths that are not divisible by <value>.\n"
                    "     Use for periodic data.\n"
                    "     Default: 1\n"
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
                if(FSIZE > 0x7FFFFFFE)
                {
                    std::fprintf(stderr, "mkcromfs: The maximum allowed fsize is 0x7FFFFFFE. You gave 0x%lX%s.\n", FSIZE, arg);
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
            case 'B':
            {
                char* arg = optarg, *pattern = arg;
                char* colon = strrchr(arg, ':');
                if(colon) *colon++ = '\0'; else colon = strchr(arg, '\0');
                long size = strtol(colon, &arg, 10);
                if(size < 8 || !*colon)
                {
                    std::fprintf(stderr, "mkcromfs: The minimum allowed bsize is 8. You gave %ld%s.\n", size, arg);
                    return -1;
                }

                BSIZE_FOR.push_back(std::make_pair(pattern, size));
                storage_opts |= CROMFS_OPT_VARIABLE_BLOCKSIZES;
                break;
            }
            case 'g':
            {
                char* arg = optarg;
                long value = strtol(arg, &arg, 10);
                if(value < 1)
                {
                    std::fprintf(stderr, "mkcromfs: The minimum allowed overlap granularity is 1. You gave %ld%s.\n", BSIZE, arg);
                    return -1;
                }
                OverlapGranularity = value;
                break;
            }
            case 'e':
            {
                DecompressWhenLookup = true;
                MaySuggestDecompression = false;
                break;
            }
            case 'l':
            {
                FollowSymlinks = true;
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
                MaySuggestDecompression = false;
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
            case 'A':
            {
                char* arg = optarg;
                long val = strtol(arg, &arg, 10);
                if(val < 0)
                {
                    std::fprintf(stderr, "mkcromfs: The minimum allowed autoindexperiod is 0. You gave %ld%s.\n", val, arg);
                    return -1;
                }
                AutoIndexPeriod = val;
                break;
            }
            case 's':
            {
                char* arg = optarg;
                long val = strtol(arg, &arg, 10);
                if(val < 0)
                {
                    std::fprintf(stderr, "mkcromfs: The minimum allowed minfreespace is 0. You gave %ld%s.\n", val, arg);
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
                if(val < 1) val = 1;
                MaxFblockCountForBruteForce = val;
                break;
            }
            case 'q':
            {
                if(DisplayBlockSelections) DisplayBlockSelections = false;
                else if(DisplayFiles) DisplayFiles = false;
                else if(DisplayEndProcess) DisplayEndProcess = false;
                else
                    std::fprintf(stderr, "mkcromfs: -qqqq not known, -qqq is the maximum.\n");
                MaySuggestDecompression = false;
                break;
            }
            case 'v':
            {
                ++LZMA_verbose;
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
            case 'S':
            {
                for(char* arg=optarg;;)
                {
                    while(*arg==' ')++arg;
                    if(!*arg) break;
                    char* comma = strchr(arg, ',');
                    if(!comma) comma = strchr(arg, '\0');
                    *comma = '\0';
                    int len = strlen(arg);
                    if(len <= 7 && !strncmp("fblocks",arg,len))
                        storage_opts |= CROMFS_OPT_SPARSE_FBLOCKS;
                    else
                    {
                        std::fprintf(stderr, "mkcromfs: Unknown option to -p (%s). See `mkcromfs --help'\n",
                            arg);
                        return -1;
                    }
                    if(!*comma) break;
                    arg=comma+1;
                }
                break;
            }
            case '4':
            {
                if(storage_opts & (CROMFS_OPT_16BIT_BLOCKNUMS | CROMFS_OPT_24BIT_BLOCKNUMS))
                {
                    std::fprintf(stderr, "mkcromfs: You can only select one of the -2, -3 and -4 options.\n");
                    return -1;
                }
                MayAutochooseBlocknumSize = false;
                break;
            }
            case '3':
            {
                storage_opts |= CROMFS_OPT_24BIT_BLOCKNUMS;
                if(!MayAutochooseBlocknumSize || (storage_opts & CROMFS_OPT_16BIT_BLOCKNUMS))
                {
                    std::fprintf(stderr, "mkcromfs: You can only select one of the -2, -3 and -4 options.\n");
                    return -1;
                }
                MayAutochooseBlocknumSize = false;
                break;
            }
            case '2':
            {
                storage_opts |= CROMFS_OPT_16BIT_BLOCKNUMS;
                if(!MayAutochooseBlocknumSize || (storage_opts & CROMFS_OPT_24BIT_BLOCKNUMS))
                {
                    std::fprintf(stderr, "mkcromfs: You can only select one of the -2, -3 and -4 options.\n");
                    return -1;
                }
                MayAutochooseBlocknumSize = false;
                break;
            }
            case 2001: // bwt
            case 2002: // mtf
            {
                std::fprintf(stderr, "mkcromfs: The --bwt and --mtf options are no longer supported.\n");
                break;
            }
            case 6001: // nopackedblocks
            {
                MayPackBlocks = false;
                break;
            }
            case 4001: // lzmafastbytes
            {
                char* arg = optarg;
                long size = strtol(arg, &arg, 10);
                if(size < 5 || size > 273)
                {
                    std::fprintf(stderr, "mkcromfs: The number of \"fast bytes\" for LZMA may be 5..273. You gave %ld%s.\n", size,arg);
                    return -1;
                }
                LZMA_NumFastBytes = size;
                break;
            }
            case 4002: // lzmabits
            {
                unsigned arg_index = 0;
                for(char* arg=optarg;;)
                {
                    if(!arg_index && !strcmp(arg, "auto")) { LZMA_HeavyCompress=1; break; }
                    if(!arg_index && !strcmp(arg, "full")) { LZMA_HeavyCompress=2; break; }
                    LZMA_HeavyCompress=0;
                    while(*arg==' ')++arg;
                    if(!*arg) break;
                    char* comma = strchr(arg, ',');
                    if(!comma) comma = strchr(arg, '\0');
                    bool last_comma = !*comma;
                    *comma = '\0';
                    long value = strtol(arg, &arg, 10);
                    long max=4;
                    switch(arg_index)
                    {
                        case 0: // pb
                            LZMA_PosStateBits = value;
                            //fprintf(stderr, "pb=%ld\n", value);
                            break;
                        case 1: // lp
                            LZMA_LiteralPosStateBits = value;
                            //fprintf(stderr, "lp=%ld\n", value);
                            break;
                        case 2: // lc
                            LZMA_LiteralContextBits = value;
                            //fprintf(stderr, "lc=%ld\n", value);
                            max=8;
                            break;
                    }
                    if(value < 0 || value > max || arg_index > 2)
                    {
                        std::fprintf(stderr, "mkcromfs: Invalid value(s) for --lzmabits. See `mkcromfs --help'\n");
                        return -1;
                    }
                    if(last_comma) break;
                    arg=comma+1;
                    ++arg_index;
                }
                break;
            }
            case 3001: // blockifybufferlength
            {
                char* arg = optarg;
                long size = strtol(arg, &arg, 10);
                if(size < 0)
                {
                    std::fprintf(stderr, "mkcromfs: The parameter blockifybufferlength may not be negative. You gave %ld%s.\n", size,arg);
                    return -1;
                }
                //BlockifyAmount1 = size;
                std::fprintf(stderr, "mkcromfs: --blockifybufferlength is obsolete and does not do anything.\n");
                break;
            }
            case 3002: // blockifybufferincrement
            {
                char* arg = optarg;
                long size = strtol(arg, &arg, 10);
                if(size < 1)
                {
                    std::fprintf(stderr, "mkcromfs: The parameter blockifybufferincrement may not be less than 1. You gave %ld%s.\n", size,arg);
                    return -1;
                }
                //BlockifyAmount2 = size;
                std::fprintf(stderr, "mkcromfs: --blockifybufferincrement is obsolete and does not do anything.\n");
                break;
            }
            case 3003: // blockifyoptimizemethod
            {
                char* arg = optarg;
                long size = strtol(arg, &arg, 10);
                if(size < 0 || size > 3)
                {
                    std::fprintf(stderr, "mkcromfs: Blockifyoptimizemethod may only be 0, 1, 2 or 3. You gave %ld%s.\n", size,arg);
                    return -1;
                }
                //TryOptimalOrganization = size;
                std::fprintf(stderr, "mkcromfs: --blockifyoptimizemethod is obsolete and does not do anything.\n");
                break;
            }
            case 3004: // blockindexmethod
            {
                char* arg = optarg;
                if(!strcmp(arg, "all"))
                    BlockHashing_Method = BlockHashing_All;
                else if(!strcmp(arg, "all2"))
                    BlockHashing_Method = BlockHashing_All_Speedup;
                else if(!strcmp(arg, "prepass"))
                    BlockHashing_Method = BlockHashing_All_Prepass;
                else if(!strcmp(arg, "blanks")
                     || !strcmp(arg, "blank"))
                    BlockHashing_Method = BlockHashing_BlanksOnly;
                else if(!strcmp(arg, "none"))
                    BlockHashing_Method = BlockHashing_None;
                else if(!strcmp(arg, "collect"))
                    BlockHashing_Method = BlockHashing_Collect;
                else if(!strcmp(arg, "collect2"))
                    BlockHashing_Method = BlockHashing_Collect_Speedup;
                else
                {
                    std::fprintf(stderr, "mkcromfs: Blockindexmethod may only be none, blanks, all, all2, prepass, collect or collect2. You gave %s.\n", arg);
                    return -1;
                }
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
            case 5001: // blockifyorder
            case 5002: // dirparseorder
            {
                char *lnk =   c==5001 ? &DataClassOrder.Symlink : &DirParseOrder.Link
                   , *dir =   c==5001 ? &DataClassOrder.Directory : &DirParseOrder.Dir
                   , *file=   c==5001 ? &DataClassOrder.File : (char*)0
                   , *inotab= c==5001 ? &DataClassOrder.Inotab : (char*)0
                   , *other=  c==5001 ? (char*)0 : &DirParseOrder.Other;
                for(char* arg=optarg;;)
                {
                    char* value_target = 0;

                    while(*arg==' ')++arg;
                    if(!*arg) break;
                    char* comma = strchr(arg, ',');
                    if(!comma) comma = strchr(arg, '\0');
                    bool last_comma = !*comma;
                    *comma = '\0';

                    char* eqpos = strchr(arg, '=');
                    if(!eqpos)
                    {
                  invalid_param_5001:
                        std::fprintf(stderr, "mkcromfs: Invalid parameters (%s). See `mkcromfs --help'\n", arg);
                    }
                    else
                    {
                        *eqpos = '\0';
                        if(!strcmp("link", arg)) value_target=lnk;
                        else if(!strcmp("dir", arg)) value_target=dir;
                        else if(!strcmp("file", arg)) value_target=file;
                        else if(!strcmp("inotab", arg)) value_target=inotab;
                        else if(!strcmp("other", arg)) value_target=other;
                        if(!value_target) goto invalid_param_5001;
                        arg = eqpos+1;
                        long value = strtol(arg, &arg, 10);
                        *value_target = value;
                        if(value < 0 || value > 5)
                        {
                            std::fprintf(stderr, "mkcromfs: Invalid parameters (%ld%s). See `mkcromfs --help'\n",
                                value, arg);
                            return -1;
                        }
                    }
                    if(last_comma) break;
                    arg=comma+1;
                }
                if(c == 5002)
                {
                    std::fprintf(stderr, "mkcromfs: --dirparseorder is obsolete and does not do anything.\n");
                }
                break;
            }
            case 5003: // nosortbyfilename
            {
                SortByFilename = false;
                break;
            }
            case 7001: // finish-interrupted
            {
                char* arg = optarg;
                resume_file_selection = arg;
                break;
            }
            case 7002: // resume-blockify
            {
                char* arg = optarg;
                ReuseListFile = arg;
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
        return -1;
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
        return -1;
    }
    if((long)OverlapGranularity >= BSIZE)
    {
        std::fprintf(stderr,
            "mkcromfs: Warning: Your overlap granularity %ld is not smaller than your\n"
            "  bsize %ld. This completely disables partial overlaps.\n",
            (long)OverlapGranularity, (long)BSIZE);
        OverlapGranularity = 0;
    }

    if(AutoIndexRatio > 0)
    {
        AutoIndexPeriod = BSIZE / AutoIndexRatio;

        if(AutoIndexPeriod < 1)
        {
            std::fprintf(stderr,
                "mkcromfs: Error: Your autoindexperiod %ld is smaller than 1.\n"
                "  Cannot comply.\n", (long)AutoIndexPeriod);
            return -1;
        }
    }
    if(AutoIndexPeriod > 0 && AutoIndexPeriod <= 4)
    {
        char Buf[256];
        if(AutoIndexPeriod == 1) std::sprintf(Buf, "for every possible byte");
        else std::sprintf(Buf, "every %u bytes", (unsigned)AutoIndexPeriod);

        std::fprintf(stderr,
            "mkcromfs: The autoindexperiod you gave, %ld, means that a _severe_ amount\n"
            "  of memory will be used by mkcromfs. An index will be added %s.\n"
            "  Just thought I should warn you.\n",
            (long)AutoIndexPeriod, Buf);
    }

    path  = argv[optind+0];
    outfn = argv[optind+1];

    if(DisplayEndProcess)
    {
        std::printf("Writing %s...\n", outfn.c_str());
    }

    int fd = open(outfn.c_str(), O_RDWR | O_CREAT | O_LARGEFILE, 0644);
    if(fd < 0)
    {
        std::perror(outfn.c_str());
        return errno;
    }

    int ExitStatus = 0;

    if(!resume_file_selection.empty())
    {
        ExitStatus = cromfs_creator::ResumeWritingFs(resume_file_selection, fd);
    }
    else
    {
        ftruncate64(fd, 0);

        (CheckSomeDefaultOptions(path));

        set_default_fblock_name_pattern();

        ExitStatus = cromfs_creator::CreateAndWriteFs(path, fd);
    }
    close(fd);

    if(DisplayEndProcess)
    {
        std::printf("End\n");
    }

    return ExitStatus;
}
