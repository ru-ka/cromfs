#ifndef bqtCromfsDataSourceDetailHH
#define bqtCromfsDataSourceDetailHH

#include "datasource.hh"

#include "lib/fadvise.hh"
#include "lib/mmapping.hh"

#include <sys/stat.h>
#include <fcntl.h> // O_RDONLY, O_LARGEFILE
#include <cstring> // std::memcpy, std::strcpy

#include <vector>

struct datasource_named: public datasource_t
{
protected:
    explicit datasource_named(const std::string& nam = "")
        : datasource_t(), name( new char[ nam.size()+1 ] )
    {
        std::strcpy(name, nam.c_str());
    }

    virtual ~datasource_named()
    {
        delete[] name;
    }

    virtual const std::string getname() const { return name; }

private:
    datasource_named(const datasource_named&);
    datasource_named& operator=(const datasource_named&);

protected:
    char* name;
};

template<typename Base>
struct datasource_with_pos: public Base
{
protected:
    template<typename T>
    explicit datasource_with_pos(const T& baseinit)
        : Base(baseinit), pos(0)
    {
    }

    virtual void rewind(uint_fast64_t p=0) { pos = p; }

    virtual void read(DataReadBuffer& buf, uint_fast64_t n, uint_fast64_t pos) = 0;

    virtual void read(DataReadBuffer& buf, uint_fast64_t n)
    {
        read(buf, n, pos);
        pos += n;
    }

private:
    uint_fast64_t pos;
};

template<typename Cont>
struct datasource_vector_base: public datasource_with_pos<datasource_named>
{
    datasource_vector_base(const std::vector<unsigned char>& vec,
                           const std::string& nam = "")
        : datasource_with_pos<datasource_named> (nam), data(vec)
    {
    }

    virtual void read(DataReadBuffer& buf, uint_fast64_t n, uint_fast64_t p)
    {
        /*
        if(pos > siz)   pos = siz;
        if(pos+n > siz) n = siz-pos;
        */
        buf.AssignRefFrom(&data[p], n);
    }
    virtual uint_fast64_t size() const { return data.size(); }

private:
    Cont data;
};

typedef datasource_vector_base<const std::vector<unsigned char> >  datasource_vector;
typedef datasource_vector_base<const std::vector<unsigned char>&>  datasource_vector_ref;

template<typename Base>
struct datasource_file: public datasource_with_pos<Base>
{
private:
    enum { BufSize = 1024*256, FailSafeMMapLength = 1048576 };
public:
    template<typename T>
    datasource_file(int fild, uint_fast64_t s, const T& baseinit)
        : datasource_with_pos<Base> (baseinit),
          fd(fild),siz(s),
          map_base(),map_length(),mmapping() { }

    virtual bool open()
    {
        mmapping.SetMap(fd, map_base = 0, map_length = siz);
        // if mmapping the entire file failed, try mmapping just a section of it
        if(!mmapping && siz >= FailSafeMMapLength)
            mmapping.SetMap(fd, map_base = 0, map_length = FailSafeMMapLength);
        rewind(0);
        return true;
    }
    virtual void close()
    {
        if(mmapping)
            mmapping.Unmap();
    }

    virtual const std::string getname() const { return "file"; }
    virtual void read(DataReadBuffer& buf, uint_fast64_t n, uint_fast64_t p)
    {
        if(mmapping && n <= map_length)
        {
            if(p < map_base || p + n - map_base > map_length)
            {
                mmapping.Unmap();
                map_base   = p &~ UINT64_C(4095);
                map_length = std::min(siz-map_base, (uint_fast64_t)FailSafeMMapLength);
                mmapping.SetMap(fd, map_base, map_length);
                if(!mmapping) goto mmap_failed;
            }

            if(p < map_base || p + n - map_base > map_length)
            {
                // mmapping failed to produce a region that satisfies the entire request.
                goto mmap_failed;
            }

            buf.AssignRefFrom(mmapping.get_ptr() + p - map_base, n);
        }
        else
        {
        mmap_failed:
            /* Note: non-buffered reading. */
            if(buf.LoadFrom(fd, n, p) < 0)
            {
                std::perror(getname().c_str());
            }
        }
    }

    virtual uint_fast64_t size() const { return siz; }
protected:
    static uint_fast64_t stat_get_size(int fild)
    {
        struct stat64 st;
        fstat64(fild, &st);
        return st.st_size;
    }
    static uint_fast64_t stat_get_size(const std::string& p)
    {
        struct stat64 st;
        stat64(p.c_str(), &st);
        return st.st_size;
    }

private:
    datasource_file(const datasource_file&);
    void operator=(const datasource_file&);

protected:
    int fd;
    uint_fast64_t siz, map_base, map_length;
    MemMappingType<true> mmapping;
};

struct datasource_file_name: public datasource_file<datasource_named>
{
    datasource_file_name(const std::string& nam, uint_fast64_t size):
        datasource_file<datasource_named> (-1, size, nam)
    {
    }
    virtual ~datasource_file_name()
    {
        close();
    }

    virtual bool open()
    {
        fd = ::open(name, O_RDONLY | O_LARGEFILE);
        if(fd < 0) { std::perror(name); return false; }
        FadviseSequential(fd, 0, siz);
        FadviseNoReuse(fd, 0, siz);
        return datasource_file::open();
    }
    virtual void close()
    {
        datasource_file::close();
        if(fd >= 0) { ::close(fd); fd = -1; }
    }
};

struct datasource_symlink: public datasource_with_pos<datasource_named>
{
    datasource_symlink(const std::string& nam, uint_fast64_t size):
        datasource_with_pos<datasource_named> (nam), linkbuf(0), siz(size)
    {
    }

    virtual uint_fast64_t size() const { return siz; }

    virtual ~datasource_symlink()
    {
        close();
        delete[] linkbuf;
    }

    virtual const std::string getname() const
    {
        return datasource_named::getname() + " (link label)";
    }

    virtual void read(DataReadBuffer& buf, uint_fast64_t n, uint_fast64_t p)
    {
        /*
        if(p > siz)   p = siz;
        if(p+n > siz) n = siz-p;
        */
        buf.AssignRefFrom(linkbuf + p, n);
    }

    virtual bool open()
    {
        if(!linkbuf) linkbuf = new unsigned char[siz];
        int res = readlink(name, (char*) linkbuf, siz);
        if(res < 0 || uint_fast64_t(res) != siz)
            { std::perror(name); return false; }
        rewind(0);
        return true;
    }
    virtual void close()
    {
        delete[] linkbuf; linkbuf = 0;
    }

private:
    unsigned char* linkbuf;
    uint_fast64_t siz;
};

#endif
