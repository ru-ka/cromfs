/*
cromfs - Copyright (C) 1992,2006 Bisqwit (http://iki.fi/bisqwit/)
Licence: GPL

cromfs-ops.cc: The filesystem access functions for Fuse. It translates
the calls of Fuse into operations performed by the cromfs class.

C++ exceptions are used to report errors: "throw errno;" is frequently
used in the cromfs class, and these functions forward it to Fuse using
the fuse_reply_err() function.

*/

#include "cromfs.hh"
#include "cromfs-ops.hh"

#include <cerrno>
#include <fcntl.h>

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
    void cromfs_uncreate(void* userdata)
    {
        cromfs* fs = (cromfs*)userdata;
        delete fs;
    }

    void cromfs_statfs(fuse_req_t req)
    {
        if(trace_ops) fprintf(stderr, "statfs\n");
        CROMFS_CTX(fs)
        
        const cromfs_superblock_internal& sblock = fs.get_superblock();
        
        struct statvfs stbuf;
        stbuf.f_bsize  = sblock.uncompressed_block_size;
        stbuf.f_frsize = sblock.uncompressed_block_size;
        stbuf.f_blocks = sblock.bytes_of_files / sblock.uncompressed_block_size;
        stbuf.f_bfree  = 0;
        stbuf.f_bavail = 0;
        stbuf.f_files  = 0;//not correct
        stbuf.f_ffree  = 0;
        stbuf.f_fsid   = 0;
        stbuf.f_flag   = ST_RDONLY;
        stbuf.f_namemax= 4096;
        fuse_reply_statfs(req, &stbuf);
        
        CROMFS_CTX_END()
    }
    
    void stat_inode(struct stat& attr, fuse_ino_t ino, const cromfs_inode_internal& i)
    {
        memset(&attr, 0, sizeof(attr));
        attr.st_dev     = 0;
        attr.st_ino     = ino;
        attr.st_mode    = i.mode;
        attr.st_nlink   = i.links;
        attr.st_uid     = i.uid ? i.uid : getuid();
        attr.st_gid     = i.gid ? i.gid : getgid();
        attr.st_size    = i.bytesize;
        attr.st_blksize = 4096;
        attr.st_blocks  = (i.bytesize + attr.st_blksize - 1) / (attr.st_blksize);
        attr.st_atime   = i.time;
        attr.st_mtime   = i.time;
        attr.st_ctime   = i.time;
        attr.st_rdev    = i.rdev;
    }

    void cromfs_lookup(fuse_req_t req, fuse_ino_t parent, const char *name)
    {
        if(trace_ops) fprintf(stderr, "lookup(%d,%s)\n", (int)parent, name);
        
        CROMFS_CTX(fs)
        
        const cromfs_dirinfo filelist = fs.read_dir(parent,  0, ~0U);
        cromfs_dirinfo::const_iterator j = filelist.find(name);
        bool found = j != filelist.end();
        
        fuse_entry_param pa;
        pa.ino        = found ? j->second : 0;
        pa.generation = j->second;
        pa.attr_timeout  = TIMEOUT_CONSTANT;
        pa.entry_timeout = TIMEOUT_CONSTANT;
        
        cromfs_inode_internal ino = fs.read_inode(j->second);
        
        if(trace_ops) fprintf(stderr, "lookup: using inode: %s\n", DumpInode(ino).c_str());
        
        stat_inode(pa.attr, j->second, ino);
        
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
        
        CROMFS_CTX(fs)

        REPLY_ERR(0);
        
        CROMFS_CTX_END()
    }

    void cromfs_readlink(fuse_req_t req, fuse_ino_t ino)
    {
        CROMFS_CTX(fs)

        const cromfs_inode_internal i = fs.read_inode_and_blocks(ino);
        char Buf[65536];
        int nread = fs.read_file_data(i, 0, (unsigned char*)Buf, sizeof(Buf)-1, "readlink");
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
                     struct fuse_file_info *fi)
    {
        if(trace_ops) fprintf(stderr, "read(%d, %ld, %u)\n", (int)ino, (long)size, (unsigned)off);
        
        CROMFS_CTX(fs)
        
        const cromfs_inode_internal i = fs.read_inode_and_blocks(ino);

        unsigned char* Buf = new unsigned char[size];
        int_fast64_t result = fs.read_file_data(i, off, Buf, size, "fileread");
        fuse_reply_buf(req, (const char*)Buf, result);
        delete[] Buf;
        
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
                        struct fuse_file_info *fi)
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
                attr.st_ino  = i->second;
                attr.st_mode = S_IFREG;
                
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
