#include "endian.hh"

#include <fcntl.h>
#include <unistd.h>
#include <errno.h>

#include "cromfs-hashmap_sparsefile.hh"
#include "../util/mkcromfs_sets.hh" // for GetTempDir

template<typename HashType, typename T>
CacheFile<HashType,T>::CacheFile(const std::string& np)
    : fds(), LargeFileOk(false), NoFilesOpen(true), NamePattern(np)
{
    for(int a=0; a<n_fds; ++a)
        fds[a] = -1;
}

template<typename HashType, typename T>
CacheFile<HashType,T>::CacheFile()
    : fds(), LargeFileOk(false), NoFilesOpen(true),
      NamePattern( GetTempDir() + std::string(
        sizeof(T) == 8 ? "/auto"
      : sizeof(T) == 4 ? "/real"
      : "/index") )
{
    for(int a=0; a<n_fds; ++a)
        fds[a] = -1;
}

/*
template<typename HashType, typename T>
void CacheFile<HashType,T>::Clone()
{
    if(LargeFileOk)
        fds[0] = dup(fds[0]);
    else
        for(int a=0; a<n_fds; ++a)
            if(fds[a] >= 0)
                fds[a] = dup(fds[a]);
}
*/

template<typename HashType, typename T>
void CacheFile<HashType,T>::GetPos(HashType index, int& fd, uint_fast64_t& pos) const
{
    /* Const version */
    pos = index;
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

template<typename HashType, typename T>
void CacheFile<HashType,T>::GetPos(HashType index, int& fd, uint_fast64_t& pos)
{
    /* Nonconst version */
    pos = index;
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
            }
            fds[fd_index] = fd;
            NoFilesOpen = false;
        }
        if(!LargeFileOk)
           pos &= (1U << 31)-1;
    }
}

template<typename HashType, typename T>
CacheFile<HashType,T>::~CacheFile()
{
//void CacheFile<HashType,T>::Close()
    if(LargeFileOk)
        close(fds[0]);
    else
        for(int a=0; a<n_fds; ++a)
            if(fds[a] >= 0)
                close(fds[a]);
}

template<typename HashType, typename T>
uint_fast64_t CacheFile<HashType,T>::GetDiskSize() const
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

template<typename HashType, typename T>
void
    CacheFile<HashType,T>::extract(HashType index, T& result) const
{
    char* Packet = (char*)&result;
    errno=0;
    int fd; uint_fast64_t pos; GetPos(index, fd, pos);
    ssize_t res = pread64(fd, Packet, RecSize, pos);
    if(res != RecSize)
    {
        /*
        fprintf(stderr, "Warning: Could not read an Index file\n");
        if(errno) perror("pread64");*/
    }
}

template<typename HashType, typename T>
void
    CacheFile<HashType,T>::set(HashType index, const T& result)
{
    const char* Packet = (const char*)&result;
    errno=0;
    int fd; uint_fast64_t pos; GetPos(index, fd, pos);
    ssize_t res = pwrite64(fd, Packet, RecSize, pos);
    if(res != RecSize)
    {
        fprintf(stderr, "Warning: Could not augment an Index file\n");
        if(errno) perror("pwrite64");
        fprintf(stderr, "Warning: Ignoring the failure and moving on\n");
    }
}


template<typename HashType, typename T>
void
    CacheFile<HashType,T>::unset(HashType index)
{
    char Packet[RecSize] = { 0 };

    errno=0;
    int fd; uint_fast64_t pos; GetPos(index, fd, pos);
    ssize_t res = pwrite64(fd, Packet, RecSize, pos);
    if(res != RecSize)
    {
        fprintf(stderr, "Warning: Could not augment an Index file\n");
        if(errno) perror("pwrite64");
        fprintf(stderr, "Warning: Ignoring the failure and moving on\n");
    }
}

template<typename HashType, typename T>
bool
    CacheFile<HashType,T>::has(HashType index) const
{
    char Packet[RecSize] = { 0 };
    errno=0;
    int fd; uint_fast64_t pos; GetPos(index, fd, pos);
    ssize_t res = pread64(fd, Packet, RecSize, pos);
    if(res != RecSize) return false;

    /*printf("has test %08X: ", index);
    for(unsigned a=0; a<RecSize; ++a) printf(" %02X", (unsigned char)Packet[a]);
    printf("\n");*/

    unsigned a=0;
    for(; (a+sizeof(long))<=RecSize; a += sizeof(long))
    {
        if(*(const long*)&Packet[a]) return true;
    }
    for(; (a+sizeof(int))<=RecSize; a += sizeof(int))
    {
        if(*(const int*)&Packet[a]) return true;
    }
    for(; (a+sizeof(char))<=RecSize; a += sizeof(char))
    {
        if(*(const char*)&Packet[a]) return true;
    }
    return false;
}

#include "cromfs-blockindex.hh" // for BlockIndexhashType, blocknum etc.
#include "newhash.h"
#define ri spfile_ri
#define ai spfile_ai
#define si spfile_si
/*
typedef CacheFile<newhash_t,cromfs_blocknum_t> ri;
template ri::CacheFile();
template ri::~CacheFile();
template void ri::extract(newhash_t,cromfs_blocknum_t&) const;
template void ri::set(newhash_t,const cromfs_blocknum_t&);
template void ri::unset(newhash_t);
template bool ri::has(newhash_t)const;
//template void ri::Close();
//template void ri::Clone();
*/
typedef CacheFile<newhash_t,cromfs_block_internal> ai;
template ai::CacheFile();
template ai::~CacheFile();
template void ai::extract(newhash_t,cromfs_block_internal&) const;
template void ai::set(newhash_t,const cromfs_block_internal&);
template void ai::unset(newhash_t);
template bool ai::has(newhash_t)const;
//template void ai::Close();
//template void ai::Clone();

typedef CacheFile<unsigned,uint_least32_t> si;
template si::CacheFile();
template si::~CacheFile();
template void si::extract(unsigned,uint_least32_t&) const;
template void si::set(unsigned,const uint_least32_t&);
template void si::unset(unsigned);
template bool si::has(unsigned)const;
//template void si::Close();
//template void si::Clone();
#undef ri
#undef ai
#undef si
