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
    autoclosefd(const autoclosefd&);
    autoclosefd&operator=(const autoclosefd&);
private:
    int fd;
};

struct autoclosefp
{
    inline autoclosefp(std::FILE* f) : fp(f) { }
    inline ~autoclosefp() { if(fp) std::fclose(fp); }
    inline operator std::FILE*() const { return fp; }
    inline bool operator! () const { return fp==0; }

private:
    autoclosefp(const autoclosefp&);
    autoclosefp&operator=(const autoclosefp&);
private:
    std::FILE* fp;
};

#endif
