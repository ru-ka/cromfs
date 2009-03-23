#ifndef bqtCromfsDataSourceDetailHH
#define bqtCromfsDataSourceDetailHH

#include "datasource.hh"

#include "lib/fadvise.hh"
#include "lib/mmapping.hh"

#include <sys/stat.h>
#include <fcntl.h> // O_RDONLY, O_LARGEFILE
#include <cstring> // std::memcpy, std::strcpy

#include <vector>

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
    datasource_vector(const datasource_vector&);
    void operator=(const datasource_vector&);

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
    datasource_vector_ref(const datasource_vector_ref&);
    void operator=(const datasource_vector_ref&);

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

private:
    datasource_file(const datasource_file&);
    void operator=(const datasource_file&);

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
    datasource_file_name(const datasource_file_name&);
    void operator=(const datasource_file_name&);
private:
    char* path;
};

struct datasource_symlink: public datasource_t
{
    datasource_symlink(const std::string& nam, uint_fast64_t size):
        path( new char [nam.size()+1] ), linkbuf(0), siz(size), pos(0)
    {
        std::strcpy(path, nam.c_str());
    }

    virtual uint_fast64_t size() const { return siz; }

    virtual ~datasource_symlink()
    {
        close();
        delete[] path;
        delete[] linkbuf;
    }
    virtual const std::string getname() const
        { return std::string(path) + " (link target)"; }

    virtual void rewind(uint_fast64_t p=0)
    {
        pos=p;
    }
    virtual void read(DataReadBuffer& buf, uint_fast64_t n)
    {
        read(buf, n, pos);
        pos += n;
    }
    virtual void read(DataReadBuffer& buf, uint_fast64_t n, uint_fast64_t pos)
    {
        if(pos > siz)   pos = siz;
        if(pos+n > siz) n = siz-pos;
        buf.AssignRefFrom(linkbuf + pos, n);
    }

    virtual bool open()
    {
        if(!linkbuf) linkbuf = new unsigned char[siz];
        int res = readlink(path, (char*) linkbuf, siz);
        if(res < 0 || uint_fast64_t(res) != siz)
            { std::perror(path); return false; }
        pos = 0;
        return true;
    }
    virtual void close()
    {
        delete[] linkbuf; linkbuf = 0;
        pos = 0;
    }

private:
    datasource_symlink(const datasource_symlink&);
    void operator=(const datasource_symlink&);
private:
    char* path;
    unsigned char* linkbuf;
    uint_fast64_t siz, pos;
};

#endif
