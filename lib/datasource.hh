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
#include <cstring> // std::memcpy

#include <vector>

struct datasource_t: public ptrable
{
    virtual const std::vector<unsigned char> read(uint_fast64_t n) = 0;
    virtual const std::vector<unsigned char> read(uint_fast64_t n, uint_fast64_t pos) = 0;
    virtual void read(DataReadBuffer& buf, uint_fast64_t n)
    {
        std::vector<unsigned char> d = read(n);
        buf.AssignCopyFrom(&d[0], d.size());
    }
    virtual void read(DataReadBuffer& buf, uint_fast64_t n, uint_fast64_t pos)
    {
        std::vector<unsigned char> d = read(n, pos);
        buf.AssignCopyFrom(&d[0], d.size());
    }

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
        : data(vec), name(nam), pos(0) { }


    virtual void rewind(uint_fast64_t p) { pos = p; }
    virtual const std::string getname() const { return name; }
    virtual const std::vector<unsigned char> read(uint_fast64_t n)
    {
        pos += n;
        return std::vector<unsigned char>(
            data.begin() + pos - n,
            data.begin() + pos);
    }
    virtual const std::vector<unsigned char> read(uint_fast64_t n, uint_fast64_t p)
    {
        return std::vector<unsigned char>(
            data.begin() + p,
            data.begin() + p + n);
    }
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
    std::string name;
    uint_fast64_t pos;
};

struct datasource_file: public datasource_t
{
private:
    enum { BufSize = 1024*256, FailSafeMMapLength = 1048576 };
protected:
    datasource_file(int fild, uint_fast64_t s): fd(fild),siz(s), buffer(),bufpos(0) { }
public:
    datasource_file(int fild)
        : fd(fild), siz(stat_get_size(fild)), buffer(),bufpos(0)
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
        else
            buffer = std::vector<unsigned char> ();
        bufpos = 0;
    }

    virtual void rewind(uint_fast64_t p=0)
    {
        bufpos=p;
        if(!mmapping)
        {
            ::lseek(fd, p, SEEK_SET);
            buffer = std::vector<unsigned char> ();
        }
    }
    virtual const std::string getname() const { return "file"; }
    virtual void read(DataReadBuffer& buf, uint_fast64_t n)
    {
        read(buf, n, bufpos);
        bufpos += n;
    }
    virtual void read(DataReadBuffer& buf, uint_fast64_t n, uint_fast64_t p)
    {
        if(mmapping)
        {
            if(p < map_base || p + n - map_base > map_length)
            {
                mmapping.Unmap();
                map_base   = p &~ UINT64_C(4095);
                map_length = std::min(siz-map_base, (uint_fast64_t)FailSafeMMapLength);
                mmapping.SetMap(fd, map_base, map_length);
                if(!mmapping) goto mmap_failed;
            }
            buf.AssignRefFrom(mmapping.get_ptr() + p - map_base, n);
        }
        else
        mmap_failed:
            datasource_t::read(buf, n, p);
    }

    virtual const std::vector<unsigned char> read(uint_fast64_t n, uint_fast64_t p)
    {
        if(mmapping)
        {
            if(p < map_base || p + n - map_base > map_length)
            {
                mmapping.Unmap();
                map_base   = p &~ UINT64_C(4095);
                map_length = std::min(siz-map_base, (uint_fast64_t)FailSafeMMapLength);
                mmapping.SetMap(fd, map_base, map_length);
                if(!mmapping) goto mmap_failed;
            }
            return std::vector<unsigned char>
                (mmapping.get_ptr() - map_base + p,
                 mmapping.get_ptr() - map_base + p + n);
        mmap_failed:;
        }

        std::vector<unsigned char> result(n);
        int res = pread64(fd, &result[0], n, p);
        if(res < 0) { std::perror(getname().c_str()); }
        return result;
    }

    virtual const std::vector<unsigned char> read(uint_fast64_t n)
    {
        if(mmapping)
        {
            std::vector<unsigned char> retval = read(n, bufpos);
            bufpos += n;
            return retval;
        }

        std::vector<unsigned char> result(n);
        uint_fast64_t result_pos    = 0;
        uint_fast64_t result_remain = n;
        while(result_remain > 0)
        {
            const size_t buf_remain = buffer.size() - bufpos;
            if(buf_remain > 0)
            {
                size_t buf_consume = buf_remain;
                if(buf_consume > result_remain) buf_consume = result_remain;
                std::memcpy(&result[result_pos], &buffer[bufpos], buf_consume);
                bufpos     += buf_consume;
                result_pos += buf_consume;
                result_remain -= buf_consume;
            }
            else
            {
                int res = ::read(fd, &buffer[0], BufSize);
                if(res < 0) { std::perror(getname().c_str()); break; }
                if(res == 0) break;
                buffer.resize(res);
            }
        }
        result.resize(result_pos);
        if(result_remain > 0)
        {
            fprintf(stderr, "%s: short read (%u, %u)", getname().c_str(),
                (unsigned)result_pos, (unsigned)n);
        }
        return result;
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
    uint_fast64_t siz;
    std::vector<unsigned char> buffer;
    uint_fast64_t bufpos, map_base, map_length;
    MemMappingType<true> mmapping;
};

struct datasource_file_name: public datasource_file
{
    datasource_file_name(const std::string& p):
        datasource_file(-1, stat_get_size(p)), path(p)
    {
    }
    virtual bool open()
    {
        fd = ::open(path.c_str(), O_RDONLY | O_LARGEFILE);
        if(fd < 0) { std::perror(path.c_str()); return false; }
        FadviseSequential(fd, 0, siz);
        FadviseNoReuse(fd, 0, siz);
        return datasource_file::open();
    }
    virtual void close()
    {
        datasource_file::close();
        if(fd >= 0) { ::close(fd); fd = -1; }
    }
    virtual ~datasource_file_name()
    {
        close();
    }
    virtual const std::string getname() const { return path; }
private:
    std::string path;
};

#endif
