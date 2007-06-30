#include "util.hh"

#ifdef __GNUC__
# define likely(x)       __builtin_expect(!!(x), 1)
# define unlikely(x)     __builtin_expect(!!(x), 0)
#else
# define likely(x)   (x)
# define unlikely(x) (x)
#endif

#include <sstream>

#include <sys/stat.h>

const std::string ReportSize(uint_fast64_t size)
{
    std::stringstream st;
    st.flags(std::ios_base::fixed);
    st.precision(2);
    if(size < 90000llu) st << size << " bytes";
    else if(size < 1500000llu)    st << (size/1e3) << " kB";
    else if(size < 1500000000llu) st << (size/1e6) << " MB";
    else if(size < 1500000000000llu) st << (size/1e9) << " GB";
    else if(size < 1500000000000000llu) st << (size/1e12) << " TB";
    else st << (size/1e15) << " PB";
    return st.str();
}

bool is_zero_block(const unsigned char* data, uint_fast64_t size)
{
#if 0
    /* easy implementation */
    for(uint_fast64_t a = 0; a < size; ++a)
        if(data[a] != 0) return false;
    return true;

#else
 #if 0
    /* Memchr might use an algorithm optimized for aligned word-size access */
    return !std::memchr(Buffer, '\0', size);
 #else
    /* attempt of a faster implementation using aligned word access where possible */
    
    typedef unsigned int aligned_type;
    const unsigned align_size = sizeof(unsigned long);
    const uint_fast64_t align_mask = align_size-1;
    
    unsigned char result = 0;
    while( ((uint_fast64_t)data) & align_mask
        && likely(size > 0)
          )
    {
        result |= *data++;
        --size;
    }
    if(likely(result > 0)) return false;
    
    unsigned num_aligned_words = (size & ~align_mask) / sizeof(aligned_type);

    const aligned_type* data_w = (const aligned_type*) data;
    while(num_aligned_words-- > 0)
    {
        if(*data_w++ != 0) return false;
    }
    size -= ((const unsigned char*)data_w) - data;
    data = (const unsigned char*)data_w;
    
    while(size-- > 0) result |= *data++;
    return result == 0;
 #endif
#endif
}

void SparseWrite(int fd,
    const unsigned char* Buffer,
    uint_fast64_t BufSize,
    uint_fast64_t WritePos)
{
    /*fprintf(stderr, "Normally, would write %04llX..%04llX from %p\n",
        WritePos, WritePos+BufSize-1, Buffer);*/
#if 0
    pwrite64(fd, Buffer, BufSize, WritePos);
#else
    const unsigned BlockSize = 1024;

    #define FlushBuf() do { \
        if(BufferedSize) \
            { \
             /*fprintf(stderr, "But writing %04llX..%04llX from %p\n", \
                 BufferedPos, BufferedPos+BufferedSize-1, BufferedBegin); */ \
             pwrite64(fd, BufferedBegin, BufferedSize, BufferedPos); } \
        BufferedSize = 0; BufferedBegin = Buffer; BufferedPos = WritePos; \
    } while(0)

    #define GoAheadBy(n) do { Buffer += (n); BufSize -= (n); WritePos += (n); } while(0)

    #define SkipBuf(n) \
        do { SkippedSize += (n); GoAheadBy(n); } while(0)
    #define AppendBuf(n) \
        do { if(SkippedSize > 0) { FlushBuf(); SkippedSize = 0; } \
             BufferedSize += (n); GoAheadBy(n); } while(0)

    const unsigned char* BufferedBegin = Buffer;
    uint_fast64_t BufferedSize = 0;
    uint_fast64_t BufferedPos  = WritePos;
    uint_fast64_t SkippedSize = 0;

    {
    uint_fast64_t NextBlockBoundary = (WritePos + BlockSize-1) & ~BlockSize;
    uint_fast64_t UnalignedRemainder = std::min(BufSize, NextBlockBoundary-WritePos);
    if(UnalignedRemainder) AppendBuf(UnalignedRemainder);
    }

    while(BufSize >= BlockSize)
        if(is_zero_block(Buffer, BlockSize))
            SkipBuf(BlockSize);
        else
            AppendBuf(BlockSize);

    if(BufSize > 0)
        if(is_zero_block(Buffer, BufSize))
            SkipBuf(BufSize);
        else
            AppendBuf(BufSize);

    FlushBuf();

    #undef AppendBuf
    #undef FlushBuf 
    #undef SkipBuf  
#endif
}

static char ftypelet(unsigned mode)
{
    /* this list of modes and chars comes from glibc, lib/filemode.c, ftypelet() */
    switch(__builtin_expect(mode & S_IFMT, S_IFREG))
    {
        case S_IFREG: return '-';
#ifdef S_ISBLK
        case S_IFBLK: return 'b';
#endif
#ifdef S_ISCHR
        case S_IFCHR: return 'c';
#endif
#ifdef S_ISDIR
        case S_IFDIR: return 'd';
#endif
#ifdef S_ISFIFO
        case S_IFIFO: return 'p';
#endif
#ifdef S_ISLNK
        case S_IFLNK: return 'l';
#endif
#ifdef S_ISSOCK
        case S_IFSOCK: return 's';
#endif
    }
#ifdef S_ISMPC
    if (S_ISMPC(mode)) return 'm';
#endif
#ifdef S_ISNWK
    if (S_ISNWK(mode)) return 'n';
#endif
#ifdef S_ISDOOR
    if (S_ISDOOR(mode)) return 'D';
#endif
#ifdef S_ISCTG
    if (S_ISCTG(mode)) return 'C';
#endif
#ifdef S_ISOFD
    if (S_ISOFD(mode)) return 'M';
#endif
#ifdef S_ISOFL
    if (S_ISOFL(mode)) return 'M';
#endif
    return '?';
}

const std::string TranslateMode(unsigned mode)
{
    char result[10];
    static const char data[] = {'-','r','x','w', 'S','T', 's','t' };
    result[0] = ftypelet(mode);
    result[1] = data[!!(mode & S_IRUSR)*1]; // -,r
    result[2] = data[!!(mode & S_IWUSR)*3]; // -,w
    result[3] = data[!!(mode & S_IXUSR)*2 + 4*!!(mode & S_ISUID)]; // -,x or S,s
    result[4] = data[!!(mode & S_IRGRP)*1]; // -,r
    result[5] = data[!!(mode & S_IWGRP)*3]; // -,w
    result[6] = data[!!(mode & S_IXGRP)*2 + 4*!!(mode & S_ISGID)]; // -,x or S,s
    result[7] = data[!!(mode & S_IROTH)*1]; // -,r
    result[8] = data[!!(mode & S_IWOTH)*3]; // -,w
    result[9] = data[!!(mode & S_IXOTH)*2 + 5*!!(mode & S_ISVTX)]; // -,x or T,t
    return std::string(result,result+10);
}
