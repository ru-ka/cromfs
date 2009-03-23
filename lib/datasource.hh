#ifndef bqtCromfsDataSourceHH
#define bqtCromfsDataSourceHH

#include "lib/datareadbuf.hh"
#include "lib/fadvise.hh"
#include "lib/mmapping.hh"
#include "autoptr"

#include <stdint.h>
#include <sys/stat.h>
#include <fcntl.h> // O_RDONLY, O_LARGEFILE
#include <string>
#include <cstring> // std::memcpy, std::strcpy

#include <vector>

struct datasource_t: public ptrable
{
    virtual void read(DataReadBuffer& buf, uint_fast64_t n) = 0;
    virtual void read(DataReadBuffer& buf, uint_fast64_t n, uint_fast64_t pos) = 0;

    virtual bool open() { return true; }
    virtual void rewind(uint_fast64_t=0) { }
    virtual void close() { }
    virtual const std::string getname() const = 0;
    virtual uint_fast64_t size() const = 0;
    virtual ~datasource_t() {};
};

struct datasource_vector: public datasource_t
{
    datasource_vector(const std::vector<unsigned char>& vec, const std::string& nam = "")
        : data(vec), name( new char[ nam.size()+1 ] ), pos(0)
    {
        std::strcpy(name, nam.c_str());
    }

    virtual ~datasource_vector()
    {
        delete[] name;
    }

    virtual void rewind(uint_fast64_t p) { pos = p; }
    virtual const std::string getname() const { return name; }
    virtual void read(DataReadBuffer& buf, uint_fast64_t n)
    {
        buf.AssignRefFrom(&data[pos], n);
        pos += n;
    }
    virtual void read(DataReadBuffer& buf, uint_fast64_t n, uint_fast64_t p)
    {
        buf.AssignRefFrom(&data[p], n);
    }
    virtual uint_fast64_t size() const { return data.size(); }
private:
    const std::vector<unsigned char> data;
    char* name;
    uint_fast64_t pos;
};

struct datasource_vector_ref: public datasource_t
{
    datasource_vector_ref(const std::vector<unsigned char>& vec, const std::string& nam = "")
        : data(vec), name( new char[ nam.size()+1 ] ), pos(0)
    {
        std::strcpy(name, nam.c_str());
    }

    virtual ~datasource_vector_ref()
    {
        delete[] name;
    }

    virtual void rewind(uint_fast64_t p) { pos = p; }
    virtual const std::string getname() const { return name; }
    virtual void read(DataReadBuffer& buf, uint_fast64_t n)
    {
        buf.AssignRefFrom(&data[pos], n);
        pos += n;
    }
    virtual void read(DataReadBuffer& buf, uint_fast64_t n, uint_fast64_t p)
    {
        buf.AssignRefFrom(&data[p], n);
    }
    virtual uint_fast64_t size() const { return data.size(); }
private:
    const std::vector<unsigned char>& data;
    char* name;
    uint_fast64_t pos;
};

struct datasource_file: public datasource_t
{
private:
    enum { BufSize = 1024*256, FailSafeMMapLength = 1048576 };
protected:
    datasource_file(int fild, uint_fast64_t s)
        : fd(fild),siz(s), pos(0),
          map_base(),map_length(),mmapping() { }
public:
    datasource_file(int fild)
        : fd(fild), siz(stat_get_size(fild)), pos(0),
          map_base(),map_length(),mmapping()
    {
        FadviseSequential(fd, 0, siz);
    }
    virtual bool open()
    {
        mmapping.SetMap(fd, map_base = 0, map_length = siz);
        // if mmapping the entire file failed, try mmapping just a section of it
        if(!mmapping && siz >= FailSafeMMapLength)
            mmapping.SetMap(fd, map_base = 0, map_length = FailSafeMMapLength);
        rewind();
        return true;
    }
    virtual void close()
    {
        if(mmapping)
            mmapping.Unmap();
        pos = 0;
    }

    virtual void rewind(uint_fast64_t p=0)
    {
        pos=p;
        if(!mmapping)
        {
            ::lseek(fd, p, SEEK_SET);
        }
    }
    virtual const std::string getname() const { return "file"; }
    virtual void read(DataReadBuffer& buf, uint_fast64_t n)
    {
        read(buf, n, pos);
        pos += n;
    }
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
protected:
    int fd;
    uint_fast64_t siz, pos, map_base, map_length;
    MemMappingType<true> mmapping;
};

struct datasource_file_name: public datasource_file
{
    datasource_file_name(const std::string& nam):
        datasource_file(-1, stat_get_size(nam)), path( new char [nam.size()+1] )
    {
        std::strcpy(path, nam.c_str());
    }
    virtual ~datasource_file_name()
    {
        close();
        delete[] path;
    }

    virtual bool open()
    {
        fd = ::open(path, O_RDONLY | O_LARGEFILE);
        if(fd < 0) { std::perror(path); return false; }
        FadviseSequential(fd, 0, siz);
        FadviseNoReuse(fd, 0, siz);
        return datasource_file::open();
    }
    virtual void close()
    {
        datasource_file::close();
        if(fd >= 0) { ::close(fd); fd = -1; }
    }
    virtual const std::string getname() const { return path; }
private:
    char* path;
};

#endif
