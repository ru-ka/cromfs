#include <openssl/md5.h>
#include <cstdio>

#undef MD5
typedef class MD5
{
private:
    unsigned char Buf[MD5_DIGEST_LENGTH];
public:
    bool operator==(const MD5& b) const { return std::memcmp(Buf,b.Buf,sizeof(Buf))==0; }
    bool operator< (const MD5& b) const { return std::memcmp(Buf,b.Buf,sizeof(Buf)) < 0; }
    bool operator!=(const MD5& b) const { return !operator==(b); }
    
    MD5(const char* data, unsigned length)
    {
        MD5_CTX ctx;
        MD5_Init(&ctx);
        MD5_Update(&ctx, data, length);
        MD5_Final(Buf, &ctx);
    }
    
    const std::string Get()
    {
        char tgt[64];
        for(unsigned a=0; a<16; ++a)
            std::sprintf(tgt+a*2, "%02X", Buf[a]);
        return tgt;
    }
    /* MD5(const std::string& s)
    {
        
    } */
} MD5c;
