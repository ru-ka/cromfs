#include <unistd.h>
#include <cstring>

struct DataReadBuffer
{
    const unsigned char* Buffer;
private:
    enum { None, Allocated } State;
public:
    DataReadBuffer() : Buffer(NULL), State(None) { }
    
    void AssignRefFrom(const unsigned char* d, unsigned)
    {
        Buffer = d; State = None;
    }
    void AssignCopyFrom(const unsigned char* d, unsigned size)
    {
        unsigned char* p = new unsigned char[size];
        std::memcpy(p, d, size);
        State = Allocated;
        Buffer = p;
    }
    int LoadFrom(int fd, uint_fast32_t size, uint_fast64_t pos = 0)
    {
        unsigned char* pp = new unsigned char[size];
        int res = pread64(fd, pp, size, pos);
        Buffer = pp;
        State = Allocated;
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
