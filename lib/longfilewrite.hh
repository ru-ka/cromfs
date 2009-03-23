#ifndef bqtLongFileWriteHH
#define bqtLongFileWriteHH

#include "endian.hh"
#include <vector>
#include <cerrno>
#include <string>

//#define USE_LIBAIO
//#define USE_AIO

/* This object tries to merge several file writes
 * together to reduce the syscall traffic.
 */
class LongFileWrite
{
public:
    explicit LongFileWrite(int fild, uint_fast64_t esize);

    explicit LongFileWrite(int fild, uint_fast64_t offset,
                           uint_fast64_t size,
                           const unsigned char* buf,
                           bool use_sparse = true);

    void write(const unsigned char* buf, uint_fast64_t size, uint_fast64_t offset,
               bool use_sparse = true);

    ~LongFileWrite() { FlushBuffer(); }
protected:
    void FlushBuffer();
    void Close();
private:
    uint_fast64_t getbufend() const { return bufpos + Buffer.size(); }
protected:
    int fd;
private:
    uint_fast64_t bufpos;
    uint_fast64_t expected_size;
    std::vector<unsigned char> Buffer;
private:
    /* no copies */
    void operator=(const LongFileWrite&);
    LongFileWrite(const LongFileWrite&);
private:
    // Avoid an accidental use of LongFileWrite with an invalid pointer
    explicit LongFileWrite(int fild, uint_fast64_t offset,
                           uint_fast64_t size,
                           bool);
};

class FileOutputter
#if !(defined(USE_LIBAIO) || defined(USE_AIO))
    : public LongFileWrite
#endif
{
public:
    explicit FileOutputter(const std::string& target, uint_fast64_t esize);
    ~FileOutputter();

#if defined(USE_LIBAIO) || defined(USE_AIO)
    void write(const unsigned char* buf, uint_fast64_t size, uint_fast64_t offset,
               bool use_sparse = true);
#endif

private:
#if defined(USE_LIBAIO) || defined(USE_AIO)
    int fd;
#endif
};

void FileOutputFlushAll();

#endif
