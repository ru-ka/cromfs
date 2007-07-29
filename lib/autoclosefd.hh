#ifndef bqtAutoCloseFD
#define bqtAutoCloseFD

#include <unistd.h>
#include <cstdio>

struct autoclosefd
{
    inline autoclosefd(int f) : fd(f) { }
    inline ~autoclosefd() { if(fd >= 0) close(fd); }
    inline operator int() const { return fd; }
private:
    int fd;
};

struct autoclosefp
{
    inline autoclosefp(std::FILE* f) : fp(f) { }
    inline ~autoclosefp() { if(fp) std::fclose(fp); }
    inline operator std::FILE*() const { return fp; }
private:
    std::FILE* fp;
};

#endif
