#ifndef bqtFadviseHH
#define bqtFadviseHH

#include "endian.hh"

void FadviseSequential(int fd, uint_fast64_t offset, uint_fast64_t length);
void FadviseRandom(int fd, uint_fast64_t offset, uint_fast64_t length);
void FadviseNoReuse(int fd, uint_fast64_t offset, uint_fast64_t length);
void FadviseWillNeed(int fd, uint_fast64_t offset, uint_fast64_t length);
void FadviseDontNeed(int fd, uint_fast64_t offset, uint_fast64_t length);

void MadviseSequential(const void* address, uint_fast64_t length);
void MadviseRandom(const void* address, uint_fast64_t length);
void MadviseWillNeed(const void* address, uint_fast64_t length);
void MadviseDontNeed(const void* address, uint_fast64_t length);

#endif
