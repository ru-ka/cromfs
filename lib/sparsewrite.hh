#include "endian.hh"

bool is_zero_block(const unsigned char* data, uint_fast64_t size);

bool SparseWrite(int fd,
    const unsigned char* Buffer,
    uint_fast64_t BufSize,
    uint_fast64_t WritePos);
