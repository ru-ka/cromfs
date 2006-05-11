#include <stdint.h>
#include <sys/stat.h>

#include <vector>

struct datasource_t
{
    virtual const std::vector<unsigned char> read(uint_least64_t n) = 0;
    virtual const uint_least64_t size() const = 0;
    virtual ~datasource_t() {};
};

struct datasource_file: public datasource_t
{
    datasource_file(int fild) : fd(fild)
    {
        struct stat64 st;
        fstat64(fd, &st);
        siz = st.st_size;
    }
    virtual const std::vector<unsigned char> read(uint_least64_t n)
    {
        std::vector<unsigned char> result(n);
        ::read(fd, &result[0], n);
        return result;
    }
    virtual const uint_least64_t size() const { return siz; }
private:
    int fd;
    uint_least64_t siz;
};

struct datasource_vector: public datasource_t
{
    datasource_vector(std::vector<unsigned char>& vec)
        : data(vec), pos(0) { }

    virtual const std::vector<unsigned char> read(uint_least64_t n)
    {
        std::vector<unsigned char> result(
            data.begin() + pos,
            data.begin() + pos + n);
        pos += n;
        return result;
    }
    virtual const uint_least64_t size() const { return data.size(); }
private:
    std::vector<unsigned char>& data;
    uint_least64_t pos;
};
