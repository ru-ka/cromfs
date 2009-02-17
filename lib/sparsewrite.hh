#include "endian.hh"
#include <sys/types.h>

bool is_zero_block(const unsigned char* data, size_t size);

bool SparseWrite(int fd,
    const unsigned char* Buffer,
    size_t BufSize,
    uint_fast64_t WritePos);
