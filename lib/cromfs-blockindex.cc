#include "lib/endian.hh"

#include <fcntl.h>

#include <sstream>
#include <stdexcept>
#include <errno.h>
#include <unistd.h>

#include "cromfs-blockindex.hh"
#include "util/mkcromfs_sets.hh"
#include "util.hh" // For ReportSize


/*
Note: These functions perform a syscall for EVERY read and write,
which is somewhat anxious for performance. However, considering
that this is a HASH structure, it is expected that there will be
little to no chances for caching. So no mmap() optimizations here.
*/

bool block_index_type::FindRealIndex(BlockIndexHashType crc, cromfs_blocknum_t& result,     size_t find_index) const
{
    if(find_index >= realindex.size()) return false;
    char Packet[4] = {0};

    int fd; uint_fast64_t pos; realindex[find_index].GetPos(crc, fd, pos);
    if(pread64(fd, Packet, 4, pos) <= 0) return false;
    result = R32(Packet);
    if(result == 0)
        return false; // hole in a file apparently

    //printf("FindRealIndex(%08X, %u)->block %u\n", crc, (unsigned)find_index, (unsigned)result);

    return true;
}

bool block_index_type::FindAutoIndex(BlockIndexHashType crc, cromfs_block_internal& result, size_t find_index) const
{
    if(find_index >= autoindex.size()) return false;
    char Packet[4+4] = {0};

    int fd; uint_fast64_t pos; autoindex[find_index].GetPos(crc, fd, pos);
    if(pread64(fd, Packet, 4+4, pos) <= 0) return false;
    result.fblocknum = R32(Packet);
    result.startoffs = R32(Packet+4);
    if(result.fblocknum == 0 && result.startoffs == 0)
        return false; // hole in a file apparently

    //printf("FindAutoIndex(%08X, %u)->fblock %u:%u\n", crc, (unsigned)find_index, (unsigned)result.fblocknum, result.startoffs);

    return true;
}

void block_index_type::AddRealIndex(BlockIndexHashType crc, cromfs_blocknum_t value)
{
    size_t find_index = 0; cromfs_blocknum_t tmp;
    while(find_index < realindex.size())
    {
        char Packet[4] = {0};
        const CacheFile<4>& constindex = realindex[find_index];
        int fd; uint_fast64_t pos; constindex.GetPos(crc, fd, pos);
        if(pread64(fd, Packet, 4, pos) <= 0) break;
        tmp = R32(Packet);
        if(tmp == 0) break;
        if(tmp == value) return;

        ++find_index;
    }
    if(find_index >= realindex.size())
    {
        printf("hash %08X demands a new RealIndex file (number %u)\n", crc, (unsigned)find_index);
        find_index = new_real();
    }

    char Packet[4];
    W32(Packet, value);
    errno=0;
TryAgain:;
    int fd; uint_fast64_t pos; realindex[find_index].GetPos(crc, fd, pos);
    ssize_t res = pwrite64(fd, Packet, 4, pos);
    if(res != 4)
    {
        fprintf(stderr, "Warning: Could not augment the RealIndex file\n");
        if(errno) perror("pwrite64");
        if(EmergencyFreeSpace(true,false)) goto TryAgain;
        fprintf(stderr, "Warning: Ignoring the failure and moving on\n");
    }
}

void block_index_type::AddAutoIndex(BlockIndexHashType crc, const cromfs_block_internal& value)
{
    size_t find_index = 0; cromfs_block_internal tmp;
    while(find_index < autoindex.size())
    {
        char Packet[4+4] = {0};
        const CacheFile<8>& constindex = autoindex[find_index];
        int fd; uint_fast64_t pos; constindex.GetPos(crc, fd, pos);
        if(pread64(fd, Packet, 4+4, pos) <= 0) break;
        tmp.fblocknum = R32(Packet);
        tmp.startoffs = R32(Packet+4);
        if(tmp.fblocknum == 0 && tmp.startoffs == 0) break;
        if(tmp == value) return;
        ++find_index;
    }
    if(find_index >= autoindex.size())
    {
        printf("hash %08X demands a new AutoIndex file (number %u)\n", crc, (unsigned)find_index);
        find_index = new_auto();
    }

    char Packet[4+4];
    W32(Packet,   value.fblocknum);
    W32(Packet+4, value.startoffs);
    errno=0;
    int fd; uint_fast64_t pos; autoindex[find_index].GetPos(crc, fd, pos);
    ssize_t res = pwrite64(fd, Packet, 4+4, pos);
    if(res != 4+4)
    {
        fprintf(stderr, "Warning: Could not augment the AutoIndex file\n");
        if(errno) perror("pwrite64");
        fprintf(stderr, "Warning: Ignoring the failure and moving on\n");
    }
}

void block_index_type::DelAutoIndex(BlockIndexHashType crc, const cromfs_block_internal& value)
{
    cromfs_block_internal tmp;
    for(size_t find_index=0; find_index < autoindex.size(); ++find_index)
    {
        if(!FindAutoIndex(crc, tmp, find_index)) break;
        if(tmp == value)
        {
            /* Ideally we would punch a hole into the file, but this is
             * the next best option
             */
            char Packet[4+4] = { 0 };
            errno=0;
            int fd; uint_fast64_t pos; autoindex[find_index].GetPos(crc, fd, pos);
            ssize_t res = pwrite64(fd, Packet, 4+4, pos);
            if(res != 4+4)
            {
                fprintf(stderr, "Warning: Could not augment the AutoIndex file\n");
                if(errno) perror("pwrite64");
                fprintf(stderr, "Warning: Ignoring the failure and moving on\n");
            }
            return;
        }
    }
}

/* Note: Not threadsafe */
size_t block_index_type::new_real()
{
    CacheFile<4> tmp( GetTempDir() + std::string("/real") );
    realindex.push_back(tmp);
    return realindex.size()-1;
}


/* Note: Not threadsafe */
size_t block_index_type::new_auto()
{
    CacheFile<8> tmp( GetTempDir() + std::string("/auto") );
    autoindex.push_back(tmp);
    return autoindex.size()-1;
}

bool block_index_type::EmergencyFreeSpace(bool Auto, bool Real)
{
    fprintf(stderr, "BlockIndex: Emergency disk space release start\n");

    bool ok = false;
    if(!autoindex.empty() && Auto)
    {
        uint_fast64_t DiskSize = autoindex[0].GetDiskSize();
        fprintf(stderr, "BlockIndex: Discarding the earliest AutoIndex file to free %s of disk space -- Note: This may degrade the compression effect!\n",
            ReportSize(DiskSize).c_str());
        autoindex[0].Close();
        autoindex.erase(autoindex.begin());
        ok = true;
    }
    else if(!realindex.empty() && Real)
    {
        uint_fast64_t DiskSize = realindex[0].GetDiskSize();
        fprintf(stderr, "BlockIndex: Discarding the earliest RealIndex file to free %s of disk space -- Note: This may degrade the compression effect significantly!\n",
            ReportSize(DiskSize).c_str());
        realindex[0].Close();
        realindex.erase(realindex.begin());
        ok = true;
    }
    else
        fprintf(stderr, "BlockIndex: Fatal error: No disk space to free\n");

    fprintf(stderr, "BlockIndex: Emergency disk space release end\n");

    return ok;
}

void block_index_type::Clone()
{
    for(size_t a=0; a<realindex.size(); ++a) { realindex[a].Clone(); }
    for(size_t a=0; a<autoindex.size(); ++a) { autoindex[a].Clone(); }
}

void block_index_type::Close()
{
    for(size_t a=0; a<realindex.size(); ++a) { realindex[a].Close(); }
    for(size_t a=0; a<autoindex.size(); ++a) { autoindex[a].Close(); }
}


/*************************************************
  Goals of this hash calculator:

  1. Minimize chances of duplicate matches
  2. Increase data locality (similar data gives similar hashes)
  3. Be fast to calculate

  Obviously, aiming for goal #2 subverts goal #1.

  Note: Among goals is _not_:
  1. Endianess safety
  The hash will never be exposed outside this program,
  so it does not need to be endian safe.

**************************************************/

#include "newhash.h"
BlockIndexHashType
    BlockIndexHashCalc(const unsigned char* buf, unsigned long size)
{
    return newhash_calc(buf, size);
}


block_index_type* block_index_global;


template<unsigned RecSize>
block_index_type::CacheFile<RecSize>::CacheFile(const std::string& np)
    : fds(), LargeFileOk(false), NoFilesOpen(true), NamePattern(np)
{
    for(int a=0; a<n_fds; ++a)
        fds[a] = -1;
}

template<unsigned RecSize>
void block_index_type::CacheFile<RecSize>::Clone()
{
    if(LargeFileOk)
        fds[0] = dup(fds[0]);
    else
        for(int a=0; a<n_fds; ++a)
            if(fds[a] >= 0)
                fds[a] = dup(fds[a]);
}

template<unsigned RecSize>
void block_index_type::CacheFile<RecSize>::GetPos(BlockIndexHashType crc, int& fd, uint_fast64_t& pos) const
{
    /* Const version */
    pos = crc;
    pos *= RecSize;
    if(LargeFileOk)
    {
        fd = fds[0];
    }
    else
    {
        int fd_index = pos >> 31;
        fd = fds[fd_index];
        pos &= (1U << 31)-1;
    }
}

template<unsigned RecSize>
void block_index_type::CacheFile<RecSize>::GetPos(BlockIndexHashType crc, int& fd, uint_fast64_t& pos)
{
    /* Nonconst version */
    pos = crc;
    pos *= RecSize;
    if(LargeFileOk)
    {
        fd = fds[0];
    }
    else
    {
        int fd_index = pos >> 31;
        fd = fds[fd_index];

        if(fds[fd_index] < 0)
        {
            // Open the file
            char Buf[128]; sprintf(Buf, "-%p-%d.dat", this, fd_index);
            std::string fn(NamePattern); fn += Buf;

            std::printf("Opening new Index file: %s\n", fn.c_str());
            fd = open(fn.c_str(), O_RDWR | O_TRUNC | O_CREAT | O_NOATIME | O_EXCL | O_LARGEFILE, 0600);
            if(fd < 0)
                perror(fn.c_str());
            else
            {
                unlink(fn.c_str()); // Ensure it gets removed once closed
#if 0
                // Check if we get a LargeFileOk...
                if(NoFilesOpen)
                {
                    char c = 0;
                    ssize_t res = pwrite64(fd,
                        &c, 1, UINT64_C(0xFFFFFFFF) * (uint_fast64_t)RecSize);
                    if(res == 1)
                    {
                        LargeFileOk = true;
                        for(int a=0; a<n_fds; ++a)
                        {
                            if(fds[a] >= 0) close(fds[a]);
                            fds[a] = fd;
                        }
                    }
                }
#endif
            }
            fds[fd_index] = fd;
            NoFilesOpen = false;
        }
        if(!LargeFileOk)
           pos &= (1U << 31)-1;
    }
}

template<unsigned RecSize>
void block_index_type::CacheFile<RecSize>::Close()
{
    if(LargeFileOk)
        close(fds[0]);
    else
        for(int a=0; a<n_fds; ++a)
            if(fds[a] >= 0)
                close(fds[a]);
}

template<unsigned RecSize>
uint_fast64_t block_index_type::CacheFile<RecSize>::GetDiskSize() const
{
    uint_fast64_t result = 0;

    for(int a=0; a<n_fds; ++a)
    {
        if(fds[a] < 0) continue;

        struct stat st;
        fstat(fds[a], &st);
        result += st.st_blocks * UINT64_C(512);
        if(a==0 && LargeFileOk) break;
    }
    return result;
}
