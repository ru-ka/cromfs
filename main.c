/*
cromfs - Copyright (C) 1992,2006 Bisqwit (http://iki.fi/bisqwit/)
Licence: GPL

main.c: The main module. This provides the bindings for the cromfs
operations for Fuse (struct fuse_lowlevel_ops), and the option parsing.

It is written in C because GNU C++ does not support the syntax
used to initialize the cromfs_oper struct.

*/

#define _LARGEFILE64_SOURCE

#include "cromfs-ops.hh"

#include <stdlib.h>
#include <unistd.h>
#include <fcntl.h>

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

int main(int argc, char *argv[])
{
    char *mountpoint;
    int fd = open(argv[1], O_RDONLY); --argc; ++argv;
    struct fuse_args args = FUSE_ARGS_INIT(argc, argv);
    
    void* userdata = cromfs_create(fd);
    if(!userdata)
    {
        return -1;
    }
    fd = -1;

    if (fuse_parse_cmdline(&args, &mountpoint, NULL, NULL) == -1)
    {
        goto out;
    }
    fd = fuse_mount(mountpoint, &args);
    if(fd == -1)
    {
        goto out;
    }

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
                err = fuse_session_loop(se);
            }
            fuse_remove_signal_handlers(se);
        }
        fuse_session_destroy(se);
    }
    close(fd);
    fuse_unmount(mountpoint, fd);
    
    cromfs_uncreate(userdata);
    
out:
    fuse_opt_free_args(&args);

    return err ? 1 : 0;
}
