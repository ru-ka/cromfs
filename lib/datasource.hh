#ifndef bqtCromfsDataSourceHH
#define bqtCromfsDataSourceHH

#include "lib/fadvise.hh"
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
    virtual bool open() { return true; }
    virtual void close() { }
    virtual const std::string getname() const = 0;
    virtual const uint_fast64_t size() const = 0;
    virtual ~datasource_t() {};
};

struct datasource_vector: public datasource_t
{
    datasource_vector(const std::vector<unsigned char>& vec, const std::string& nam = "")
        : data(vec), name(nam), pos(0) { }

    virtual const std::string getname() const { return name; }
    virtual const std::vector<unsigned char> read(uint_fast64_t n)
    {
        std::vector<unsigned char> result(
            data.begin() + pos,
            data.begin() + pos + n);
        pos += n;
        return result;
    }
    virtual const uint_fast64_t size() const { return data.size(); }
private:
    const std::vector<unsigned char> data;
    std::string name;
    uint_fast64_t pos;
};

struct datasource_file: public datasource_t
{
private:
    static const size_t BufSize = 1024*256;
protected:
    datasource_file(int fild, uint_fast64_t s): fd(fild),siz(s), buffer(),bufpos(0) { }
public:
    datasource_file(int fild) : fd(fild), siz(stat_get_size(fild)), buffer(),bufpos(0)
    {
        FadviseSequential(fd, 0, siz);
    }
    virtual const std::string getname() const { return "file"; }
    virtual const std::vector<unsigned char> read(uint_fast64_t n)
    {
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
                bufpos = 0;
                buffer.resize(BufSize);
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
    virtual const uint_fast64_t size() const { return siz; }
    virtual void close() { buffer = std::vector<unsigned char> (); bufpos = 0; }
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
    std::vector<unsigned char> buffer; size_t bufpos;
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
        FadviseWillNeed(fd, 0, siz);
        return true;
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
