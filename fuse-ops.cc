/*
cromfs - Copyright (C) 1992,2009 Bisqwit (http://iki.fi/bisqwit/)
Licence: GPL3

cromfs-ops.cc: The filesystem access functions for Fuse. It translates
the calls of Fuse into operations performed by the cromfs class.

C++ exceptions are used to report errors: "throw errno;" is frequently
used in the cromfs class, and these functions forward it to Fuse using
the fuse_reply_err() function.

*/

#include "cromfs.hh"
#include "fuse-ops.hh"

#include <cerrno>
#include <fcntl.h>
#include <cstring>
#include <sys/statvfs.h>
#include <cstring> /* for std::memset */


#define CROMFS_CTXP(obj,userdata) \
    cromfs& obj = (*(cromfs*)(userdata))

#define CROMFS_CTX(obj) \
    CROMFS_CTXP(obj, fuse_req_userdata(req)); \
    try {

#define REPLY_ERR(err) fuse_reply_err(req, err)
/*#define REPLY_ERR(err) throw err*/

#define CROMFS_CTX_END() \
    } \
    catch(cromfs_exception e) \
    { \
        REPLY_ERR(e); \
        return; \
    } \
    catch(std::bad_alloc) \
    { \
        REPLY_ERR(ENOMEM); \
    }/*catch(char){}*/

#define READDIR_DEBUG   0

#define LIGHTWEIGHT_READDIR 0

static const double TIMEOUT_CONSTANT = 0x7FFFFFFF;

static bool trace_ops = false;

extern "C" {

    void* cromfs_create(int fd)
    {
        cromfs* fs = NULL;
        try
        {
            if(isatty(fd))
            {
                throw EBADFD;
            }
            fs = new cromfs(fd);
        }
        /*catch(char){}*/
        catch(cromfs_exception e)
        {
            errno=e;
            perror("cromfs");
            return NULL;
        }
        return (void*)fs;
    }
    void cromfs_initialize(void* userdata)
    {
        cromfs* fs = (cromfs*)userdata;
        if(fs) fs->Initialize();
    }
    void cromfs_uncreate(void* userdata)
    {
        cromfs* fs = (cromfs*)userdata;
        delete fs;
    }

    void cromfs_statfs(fuse_req_t req/*, fuse_ino_t unused_ino*/)
    {
        if(trace_ops) fprintf(stderr, "statfs\n");
        CROMFS_CTX(fs)

        const cromfs_superblock_internal& sblock = fs.get_superblock();

        struct statvfs stbuf;
        stbuf.f_bsize  = sblock.bsize;
        stbuf.f_frsize = sblock.bsize;
        stbuf.f_blocks = sblock.bytes_of_files / sblock.bsize;
        stbuf.f_bfree  = 0;
        stbuf.f_bavail = 0;
        stbuf.f_files  = 0;//not correct
        stbuf.f_ffree  = 0;
        stbuf.f_fsid   = 0;
        stbuf.f_flag   = ST_RDONLY;
        stbuf.f_namemax= (unsigned long int)(~0UL);
        fuse_reply_statfs(req, &stbuf);

        CROMFS_CTX_END()
    }

    static void stat_inode(struct stat& attr, fuse_ino_t ino, const cromfs_inode_internal& i)
    {
        using namespace std;
        // ^Because gcc gives me a "error: 'memset' is not a member of 'std'"
        // Yet, cstring is included

        memset(&attr, 0, sizeof(attr));
        attr.st_dev     = 0;
        attr.st_ino     = ino;
        attr.st_mode    = i.mode;
        attr.st_nlink   = i.links;
        attr.st_uid     = i.uid ? i.uid : getuid();
        attr.st_gid     = i.gid ? i.gid : getgid();
        attr.st_size    = i.bytesize;
        attr.st_blksize = 4096;

        const unsigned unit = 512; // attr.st_blksize;
        // For some reason, 512 seems to be the right
        // value that has "du" work right, not 4096.

        attr.st_blocks  = (i.bytesize + unit - 1) / (unit);
        attr.st_atime   = i.time;
        attr.st_mtime   = i.time;
        attr.st_ctime   = i.time;
        attr.st_rdev    = i.rdev;
    }

    void cromfs_lookup(fuse_req_t req, fuse_ino_t parent, const char *name)
    {
        if(trace_ops) fprintf(stderr, "lookup(%d,%s)\n", (int)parent, name);

        CROMFS_CTX(fs)

        cromfs_inodenum_t inonum = fs.dir_lookup(parent, name);
        fuse_entry_param pa;
        pa.ino        = inonum;
        pa.generation = inonum;
        pa.attr_timeout  = TIMEOUT_CONSTANT;
        pa.entry_timeout = TIMEOUT_CONSTANT;

        if(inonum != 0)
        {
            cromfs_inode_internal ino = fs.read_inode(inonum);
            if(trace_ops) fprintf(stderr, "lookup: using inode: %s\n", DumpInode(ino).c_str());
            stat_inode(pa.attr, inonum, ino);
        }

        fuse_reply_entry(req, &pa);

        CROMFS_CTX_END()
    }

    void cromfs_getattr(fuse_req_t req, fuse_ino_t ino, struct fuse_file_info *)
    {
        if(trace_ops) fprintf(stderr, "getattr(%d)\n", (int)ino);

        CROMFS_CTX(fs)

        const cromfs_inode_internal i = fs.read_inode(ino);
        struct stat attr;

        stat_inode(attr, ino, i);
        fuse_reply_attr(req, &attr, TIMEOUT_CONSTANT);

        CROMFS_CTX_END()
    }

    void cromfs_access(fuse_req_t req, fuse_ino_t ino, int mask)
    {
        if(trace_ops) fprintf(stderr, "access(%d,%d)\n", (int)ino, mask);

        /*CROMFS_CTX(fs)*/

        REPLY_ERR(0);

        /*CROMFS_CTX_END()*/
    }

    void cromfs_readlink(fuse_req_t req, fuse_ino_t ino)
    {
        CROMFS_CTX(fs)

        char Buf[65536];
        int nread = fs.read_file_data(ino, 0, (unsigned char*)Buf, sizeof(Buf)-1, "readlink");
        if(nread < 0)
        {
            REPLY_ERR(nread);
            return;
        }
        Buf[nread] = 0;
        fuse_reply_readlink(req, Buf);

        CROMFS_CTX_END()
    }

    void cromfs_open(fuse_req_t req, fuse_ino_t ino, struct fuse_file_info *fi)
    {
        if(trace_ops) fprintf(stderr, "open(%d)\n", (int)ino);

        CROMFS_CTX(fs)

        fi->keep_cache = 1;
        const cromfs_inode_internal i = fs.read_inode(ino);
        if(S_ISDIR(i.mode))
            REPLY_ERR(EISDIR);
        else if ((fi->flags & 3) != O_RDONLY)
            REPLY_ERR(EACCES);
        else
            fuse_reply_open(req, fi);

        CROMFS_CTX_END()
    }

    void cromfs_read(fuse_req_t req, fuse_ino_t ino, size_t size, off_t off,
                     struct fuse_file_info */*fi*/)
    {
        if(trace_ops) fprintf(stderr, "read(%d, %ld, %u)\n", (int)ino, (long)size, (unsigned)off);

        CROMFS_CTX(fs)

        std::vector<unsigned char> Buf(size);

        int_fast64_t result = fs.read_file_data(ino, off, &Buf[0], size, "fileread");
        fuse_reply_buf(req, (const char*)&Buf[0], result);

        CROMFS_CTX_END()
    }

    void cromfs_opendir(fuse_req_t req, fuse_ino_t ino, struct fuse_file_info *fi)
    {
        if(trace_ops) fprintf(stderr, "opendir(%d)\n", (int)ino);

        CROMFS_CTX(fs)
        fi->keep_cache = 1;
        const cromfs_inode_internal i = fs.read_inode(ino);
        if(!S_ISDIR(i.mode))
            REPLY_ERR(ENOTDIR);
        else
            fuse_reply_open(req, fi);

        CROMFS_CTX_END()
    }

    void cromfs_readdir(fuse_req_t req, fuse_ino_t ino, size_t size, off_t off,
                        struct fuse_file_info */*fi*/)
    {
        if(trace_ops) fprintf(stderr, "readdir(%d)\n", (int)ino);

        CROMFS_CTX(fs)
        if(size <= 0)
        {
            REPLY_ERR(EINVAL);
            return;
        }

#if FUSE_VERSION >= 26
        std::vector<char> dirbuf(size);
#else
        std::vector<char> dirbuf(size + 4096);
#endif

        /* Estimate that each dir entry is about 64 bytes in size by average */
        unsigned size_per_elem = 64;

        unsigned dirbuf_head = 0;

        /* Just in case, read the entire directory. There is very
         * rarely a need to readdir() only a portion of it.
         * This way, we will have it in cache.
         */
        fs.read_dir(ino, 0, (uint_fast32_t)~0U);

        while(size > 0)
        {
            unsigned dir_count = (size + size_per_elem - 1) / size_per_elem;

#if READDIR_DEBUG
            fprintf(stderr, "querying read_dir(%d,%u,%u)\n", (int)ino,off,dir_count);
#endif
            cromfs_dirinfo dirinfo = fs.read_dir(ino, off, dir_count);
            for(cromfs_dirinfo::const_iterator
                i = dirinfo.begin();
                i != dirinfo.end();
                ++i)
            {
#if READDIR_DEBUG
                fprintf(stderr, "- encoding dir entry @%ld '%s' (%ld)\n",
                    (long)off, i->first.c_str(),
                    (long) i->second);
#endif

                struct stat attr;
            #if LIGHTWEIGHT_READDIR
                attr.st_ino  = i->second;
                attr.st_mode = S_IFREG;
            #else
                /* Apparently GNU find doesn't function without this. */
                /* This is used to populate the d_type field in readdir results. */
                const cromfs_inode_internal in = fs.read_inode(i->second);
                stat_inode(attr, i->second, in);
            #endif

                ++off;

#if FUSE_VERSION >= 26
                size_t ent_size = fuse_add_direntry(
                    req,
                    &dirbuf[dirbuf_head], size,
                    i->first.c_str(),
                    &attr,
                    off);
#else
                size_t ent_size = fuse_add_dirent(
                    &dirbuf[dirbuf_head],
                    i->first.c_str(),
                    &attr,
                    off) - &dirbuf[dirbuf_head];
#endif

                if(ent_size > size)
                {
                    size=0;
                    break;
                }
                size        -= ent_size;
                dirbuf_head += ent_size;
            }
            if(dirinfo.empty()) break;
        }
        fuse_reply_buf(req, &dirbuf[0], dirbuf_head);

        CROMFS_CTX_END()
    }
}
