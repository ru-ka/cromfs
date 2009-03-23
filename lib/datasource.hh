#ifndef bqtCromfsDataSourceHH
#define bqtCromfsDataSourceHH

#include "endian.hh"
#include "lib/datareadbuf.hh"
#include <string>

struct datasource_t
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

#endif
