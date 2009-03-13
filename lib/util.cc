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
#include <cstring>

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

static char ftypelet(unsigned mode)
{
    /* this list of modes and chars comes from glibc, lib/filemode.c, ftypelet() */
    static const char modes[1 << (3+1)] =
    {
    //  000xxxx 001xxxx 002xxxx 003xxxx  004xxxx  005xxxx 006xxxx 007xxxx
        '?'/**/,'p',    'c',    'm',     'd',     '?',    'b',    'm',
    //  010xxxx 011xxxx 012xxxx 013xxxx  014xxxx  015xxxx 016xxxx 017xxxx
        '-',    '?',    'l',    '?',     's',     'D',    '?',    '?'

        // Recognized:
        // 001: IFIFO
        // 002: IFCHR
        // 003: IFMPC (not Linux)
        // 004: IFDIR
        // 006: IFBLK
        // 007: IFMPB (coherent)
        // 010: IFREG
        // 012: IFLNK
        // 014: IFSOCK
        // 015: IFDOOR (not Linux)
        //
        // Don't know mode characters for these:
        // 005: IFNAM (xenix)
        // 011: IFCMP (compressed,VxFS)
        // 013: IFSHAD (solaris)
        // 016: IFWHT (BSD whiteout)
        //
        // Don't know bitmasks for these (known by glibc):
        //      ISWNK ('n')
        //      ISCTG ('C')
        //      ISOFD ('M')
        //      ISOFL ('M')
    };
    return modes[(mode & S_IFMT) >> 12];
}

const std::string TranslateMode(unsigned mode)
{
    static const char rw[2*4] =
    {// two-byte options. +1 = write; +2 = read
     '-','-', // no r and no w
     '-','w', // w and no r
     'r','-', // r and no w
     'r','w', // r and w
    };
    static const char sx[2*3] =
    {// one-byte items. +1 = execute; +2 = svtx; +4 = suid/sgid
     '-','x', // no x; x
     'T','t', // svtx and no x; vtx and x
     'S','s'};// suid and no x; suid and x  (same for sgid)
    char destptr[10] = {ftypelet(mode)};
    #define set(offs,ndset, src,nsrc) std::memcpy(destptr+offs, src,nsrc)
    unsigned mdlo = mode/* & 0777*/, mdhi = mode / (01000/2);
    set(7,2, rw+(mdlo&06), 2); mdlo >>= 3; // other rw
    set(4,2, rw+(mdlo&06), 2); mdlo >>= 3; // group rw
    set(1,2, rw+(mdlo&06), 2);             // user  rw
    set(9,1, sx+(mode&1) + (mdhi&2), 1); mode >>= 3; mdhi >>= 1; // other x & svtx
    set(6,1, sx+(mode&1) + (mdhi&2), 1); mode >>= 3;             // group x & sgid
    set(3,1, sx+(mode&1) + (mdhi&4), 1);                         // user  x & suid
    #undef set
    return std::string(destptr, destptr+10);
}
