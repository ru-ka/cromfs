/*
cromfs - Copyright (C) 1992,2007 Bisqwit (http://iki.fi/bisqwit/)
Licence: GPL

main.c: The main module. This provides the bindings for the cromfs
operations for Fuse (struct fuse_lowlevel_ops), and the option parsing.

It is written in C because GNU C++ does not support the syntax
used to initialize the cromfs_oper struct.

*/

#define _LARGEFILE64_SOURCE

#include "cromfs-ops.hh"

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>

static const struct fuse_lowlevel_ops cromfs_oper =
{
    .statfs  = cromfs_statfs,
    
    .lookup  = cromfs_lookup,
    
    .readlink = cromfs_readlink,
    
    .getattr  = cromfs_getattr,
    .access  = cromfs_access,
    
    .readlink = cromfs_readlink,
    
    .open    = cromfs_open,
    .read    = cromfs_read,
    
    .opendir = cromfs_opendir,
    .readdir = cromfs_readdir
};

static int foreground = 0;
static int multithreaded = 0;

int main(int argc, char *argv[])
{
    char *mountpoint = NULL;
    int fd = open(argv[1], O_RDONLY);
    if(fd >= 0) { --argc; ++argv; }
    
    struct fuse_args args = FUSE_ARGS_INIT(argc, argv);
    
    if(fuse_parse_cmdline(&args, &mountpoint, &multithreaded, &foreground) == -1)
    {
        if(fd >= 0) close(fd);
        goto out;
    }

    void* userdata = cromfs_create(fd);
    if(!userdata)
    {
        fprintf(stderr, "cromfs_create failed. Usage: cromfs-driver <image> [<options>] <directory>\n");
        return -1;
    }

    fd = fuse_mount/*_compat25*/(mountpoint, &args);
    if(fd == -1)
    {
        goto out;
    }

#if 0
    fprintf(stderr, "fuse_mount gives fd %d\n", fd);
#endif

    int err = -1;
    struct fuse_session *se
        = fuse_lowlevel_new(&args, &cromfs_oper, sizeof(cromfs_oper),
                            userdata);
    if (se != NULL)
    {
        if (fuse_set_signal_handlers(se) != -1)
        {
            struct fuse_chan *ch = fuse_kern_chan_new(fd);
            if (ch != NULL)
            {
                fuse_session_add_chan(se, ch);
                
                if(foreground)
                    fprintf(stderr, "ready\n");
                fuse_daemonize(foreground);

                err = fuse_session_loop(se);
            }
            fuse_remove_signal_handlers(se);
        }
        fuse_session_destroy(se);
    }
    close(fd);
 #if FUSE_VERSION >= 26
    fuse_unmount(mountpoint, fd);
 #else
    fuse_unmount(mountpoint);
 #endif

    cromfs_uncreate(userdata);
    
out:
    fuse_opt_free_args(&args);

    return err ? 1 : 0;
}
