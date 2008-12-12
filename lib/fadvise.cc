#define _XOPEN_SOURCE 600
#include <fcntl.h> // For posix_fadvise
#include <sys/mman.h> //  for madvise

#include "fadvise.hh"

void FadviseSequential(int fd, uint_fast64_t offset, uint_fast64_t length)
{
    posix_fadvise(fd,offset,length, POSIX_FADV_SEQUENTIAL);
}

void FadviseRandom(int fd, uint_fast64_t offset, uint_fast64_t length)
{
    posix_fadvise(fd,offset,length, POSIX_FADV_RANDOM);
}

void FadviseNoReuse(int fd, uint_fast64_t offset, uint_fast64_t length)
{
    posix_fadvise(fd,offset,length, POSIX_FADV_NOREUSE);
}

void FadviseWillNeed(int fd, uint_fast64_t offset, uint_fast64_t length)
{
    posix_fadvise(fd,offset,length, POSIX_FADV_WILLNEED);
}

void FadviseDontNeed(int fd, uint_fast64_t offset, uint_fast64_t length)
{
    posix_fadvise(fd,offset,length, POSIX_FADV_DONTNEED);
}

void MadviseSequential(const void* address, uint_fast64_t length)
{
    madvise(const_cast<void*> (address), length, MADV_SEQUENTIAL);
}

void MadviseRandom(const void* address, uint_fast64_t length)
{
    madvise(const_cast<void*> (address), length, MADV_RANDOM);
}

void MadviseWillNeed(const void* address, uint_fast64_t length)
{
    madvise(const_cast<void*> (address), length, MADV_WILLNEED);
}

void MadviseDontNeed(const void* address, uint_fast64_t length)
{
    madvise(const_cast<void*> (address), length, MADV_DONTNEED);
}
