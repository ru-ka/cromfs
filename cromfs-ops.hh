#define FUSE_USE_VERSION 25
#include <fuse_lowlevel.h>

#ifdef __cplusplus
extern "C" {
#endif

void* cromfs_create(int fd);
void cromfs_uncreate(void* userdata);


void cromfs_statfs(fuse_req_t req, fuse_ino_t ino);
void cromfs_lookup(fuse_req_t req, fuse_ino_t parent, const char *name);
void cromfs_getattr(fuse_req_t req, fuse_ino_t ino, struct fuse_file_info *);
void cromfs_access(fuse_req_t req, fuse_ino_t ino, int mask);
void cromfs_readlink(fuse_req_t req, fuse_ino_t ino);
void cromfs_open(fuse_req_t req, fuse_ino_t ino, struct fuse_file_info *fi);
void cromfs_read(fuse_req_t req, fuse_ino_t ino, size_t size, off_t off,
                 struct fuse_file_info *fi);
void cromfs_opendir(fuse_req_t req, fuse_ino_t ino, struct fuse_file_info *fi);
void cromfs_readdir(fuse_req_t req, fuse_ino_t ino, size_t size, off_t off,
                    struct fuse_file_info *fi);

#ifdef __cplusplus
}
#endif
