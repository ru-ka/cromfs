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
void CacheFile<HashType,T>::GetPos(HashType crc, int& fd, uint_fast64_t& pos) const
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

template<typename HashType, typename T>
void CacheFile<HashType,T>::GetPos(HashType crc, int& fd, uint_fast64_t& pos)
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
    CacheFile<HashType,T>::extract(HashType crc, T& result) const
{
    char* Packet = (char*)&result;
    errno=0;
    int fd; uint_fast64_t pos; GetPos(crc, fd, pos);
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
    CacheFile<HashType,T>::set(HashType crc, const T& result)
{
    const char* Packet = (const char*)&result;
    errno=0;
    int fd; uint_fast64_t pos; GetPos(crc, fd, pos);
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
    CacheFile<HashType,T>::unset(HashType crc)
{
    char Packet[RecSize] = { 0 };

    errno=0;
    int fd; uint_fast64_t pos; GetPos(crc, fd, pos);
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
    CacheFile<HashType,T>::has(HashType crc) const
{
    char Packet[RecSize] = { 0 };
    errno=0;
    int fd; uint_fast64_t pos; GetPos(crc, fd, pos);
    ssize_t res = pread64(fd, Packet, RecSize, pos);
    if(res != RecSize) return false;

    unsigned a=0;
    for(; a<RecSize; a += sizeof(unsigned long))
    {
        if(*(const unsigned long*)&Packet[a]) return true;
    }
    for(; a<RecSize; a += sizeof(unsigned))
    {
        if(*(const unsigned*)&Packet[a]) return true;
    }
    for(; a<RecSize; a += sizeof(char))
    {
        if(*(const char*)&Packet[a]) return true;
    }
    return false;
}

#include "cromfs-blockindex.hh" // for BlockIndexhashType, blocknum etc.
/*
typedef CacheFile<BlockIndexHashType,cromfs_blocknum_t> ri;
template ri::CacheFile();
template ri::~CacheFile();
template void ri::extract(BlockIndexHashType,cromfs_blocknum_t&) const;
template void ri::set(BlockIndexHashType,const cromfs_blocknum_t&);
template void ri::unset(BlockIndexHashType);
template bool ri::has(BlockIndexHashType)const;
//template void ri::Close();
//template void ri::Clone();
*/
typedef CacheFile<BlockIndexHashType,cromfs_block_internal> ai;
template ai::CacheFile();
template ai::~CacheFile();
template void ai::extract(BlockIndexHashType,cromfs_block_internal&) const;
template void ai::set(BlockIndexHashType,const cromfs_block_internal&);
template void ai::unset(BlockIndexHashType);
template bool ai::has(BlockIndexHashType)const;
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
