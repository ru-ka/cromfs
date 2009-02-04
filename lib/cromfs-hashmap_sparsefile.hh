#ifndef bqtCromfsHashMapSparseFile
#define bqtCromfsHashMapSparseFile

#include <string>
#include "endian.hh"

template<typename HashType, typename T>
class CacheFile
{
private:
    enum { RecSize = sizeof(T) };
public:
    CacheFile();
    ~CacheFile();

    void extract(HashType crc, T& result)       const;
    void     set(HashType crc, const T& value);
    void   unset(HashType crc);
    bool     has(HashType crc) const;

private:
    explicit CacheFile(const std::string& np);
    void GetPos(HashType crc, int& fd, uint_fast64_t& pos) const;
    void GetPos(HashType crc, int& fd, uint_fast64_t& pos);
    uint_fast64_t GetDiskSize() const;
private:
    enum { n_fds = RecSize*2 };
    int fds[n_fds];
    bool LargeFileOk, NoFilesOpen;
    std::string NamePattern;
};

#endif
