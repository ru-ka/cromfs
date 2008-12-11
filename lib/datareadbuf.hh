#ifndef bqtDataReadBuf
#define bqtDataReadBuf

#include <unistd.h>
#include <cstring>

struct DataReadBuffer
{
    const unsigned char* Buffer;
private:
    enum { None, Allocated } State;
public:
    DataReadBuffer() : Buffer(NULL), State(None)
    {
    #pragma omp flush(State,Buffer)
    }

    void AssignRefFrom(const unsigned char* d, unsigned)
    {
        Buffer = d; State = None;
    #pragma omp flush(State,Buffer)
    }
    void AssignCopyFrom(const unsigned char* d, unsigned size)
    {
        unsigned char* p = new unsigned char[size];
        std::memcpy(p, d, size);
        State = Allocated;
        Buffer = p;
    #pragma omp flush(State,Buffer)
    }
    int LoadFrom(int fd, uint_fast32_t size, uint_fast64_t pos = 0)
    {
        unsigned char* pp = new unsigned char[size];
        int res = pread64(fd, pp, size, pos);
        Buffer = pp;
        State = Allocated;
    #pragma omp flush(State,Buffer)
        return res;
    }
    ~DataReadBuffer()
    {
        switch(State)
        {
            case Allocated: delete[] Buffer; break;
            case None: ;
        }
    }
private:
    void operator=(const DataReadBuffer&);
    DataReadBuffer(const DataReadBuffer&);
};

#endif
