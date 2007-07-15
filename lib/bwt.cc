#include "endian.hh"

#include <vector>
#include <algorithm>

#include "bwt.hh"

#ifdef __GNUC__
# define likely(x)       __builtin_expect(!!(x), 1)
# define unlikely(x)     __builtin_expect(!!(x), 0)
#else
# define likely(x)   (x)
# define unlikely(x) (x)
#endif

#define USE_GRZIP_ENCODE
#define USE_GRZIP_DECODE

namespace
{
    typedef unsigned char byte;

#if defined(USE_GRZIP_ENCODE) || defined(USE_GRZIP_DECODE)
 #include "grzip/BWT.c"
#endif

#ifndef USE_GRZIP_ENCODE
    class BWTComparator
    {
        const byte* const input;
        const size_t size;
        
        bool worst_case;

     public:
        BWTComparator(const byte* const i, size_t s): input(i), size(s)
        {
            worst_case = false;
            size_t num_words = size / sizeof(unsigned);
            size_t num_same  = 0;
            const unsigned* wordptr = (const unsigned *)input;
            for(size_t a = 1; a < num_words; ++a)
                if(wordptr[a]==wordptr[a-1])
                {
                    ++num_same;
                    if(num_same >= num_words/20)
                        { worst_case = true; break; }
                }
        }

        inline bool operator()(size_t li, size_t ri) const
        {
            /*
                basically, this is the same as
                  std::string tmp(input,size); tmp+=tmp;
                  return tmp.compare(li,size, tmp, ri,size) < 0;
            */
            return worst_case ? Com2(li,ri) : Com1(li,ri);
            //return Com1(li,ri);
        }
        
        bool Com1(size_t li, size_t ri) const
        {
            if(li == ri) return false; // equal because the positions are same.
            size_t amount = size;
            while(input[li] == input[ri])
            {
                if(++li == size) li = 0;
                if(++ri == size) ri = 0;
                if(--amount == 0) return false; // equal
            }
            return input[li] < input[ri];
        }
        bool Com2(size_t li, size_t ri) const
        {
            if(unlikely(li == ri))
                return false; // equal because the positions are same.
               
            size_t remain = size;
            while(remain > 0)
            {
                size_t go = std::min(remain, size - std::max(li,ri));
                
                {int c = std::memcmp(input+li, input+ri, go);
                 if(likely(c != 0)) return c < 0;
                }
                
                if((li+=go) >= size) li = 0;
                if((ri+=go) >= size) ri = 0;
                remain -= go; 
            }
            return false;
        }
    };
    
    static size_t BWT_encode(byte* encoded,
                             const byte* const input,
                             size_t size)
    {
        std::vector<size_t> indices(size);
        for(size_t i = 0; i < size; ++i) indices[i] = i;
        std::sort(indices.begin(), indices.end(), BWTComparator(input, size));

        size_t primaryIndex = 0;
        size_t i;
        for(i = 0; i < size; ++i)
        {
            encoded[i] = input[(indices[i] + size - 1) % size];
            if(indices[i] == 1) { primaryIndex = i; break; }
        }
        // very minor optimization: do the rest of the loop without the if()
        for(; i < size; ++i)
            encoded[i] = input[(indices[i] + size - 1) % size];
        return primaryIndex;
    }
#else
    static size_t BWT_encode(byte* encoded,
                             const byte* const input,
                             size_t size)
    {
        std::vector<byte> temp(input, input+size);
        return GRZip_StrongBWT_Encode(&temp[0], size, encoded);
    }
#endif

#ifndef USE_GRZIP_DECODE
    static void BWT_decode(byte* decoded,
                           const byte* const encoded,
                           size_t size,
                           const size_t primaryIndex)
    {
        size_t buckets[256] = { 0 };
        std::vector<size_t> indices(size);
        std::vector<byte> F(size);

        for(size_t i = 0; i < size; ++i)
            ++buckets[encoded[i]];

        for(size_t i = 0, k = 0; i < 256; ++i)
            for(size_t j = 0; j < buckets[i]; ++j)
                F[k++] = byte(i);

        for(size_t i = 0, j = 0; i < 256; ++i)
        {
            while(j < size && i > F[j]) ++j;
            buckets[i] = j;
        }

        for(size_t i = 0; i < size; ++i)
            indices[buckets[encoded[i]]++] = i;

        for(size_t i = 0, j = primaryIndex; i < size; ++i, j = indices[j])
            decoded[i] = encoded[j];
    }
#else
    static void BWT_decode(byte* decoded,
                           const byte* const encoded,
                           size_t size,
                           const size_t primaryIndex)
    {
        std::memcpy(decoded, encoded, size);
        GRZip_StrongBWT_Decode(decoded, size, primaryIndex);
    }
#endif
}

///////////// Frontend functions

// Separate index versions

const std::vector<byte>
    BWT_encode(const byte* const input, size_t size,
               size_t& primaryIndex)
{
    std::vector<byte> encoded ( size );
    primaryIndex = BWT_encode(&encoded[0], input, size);
    return encoded;
}

const std::vector<byte>
    BWT_decode(const byte* const encoded, size_t size,
               const size_t primaryIndex)
{
    std::vector<byte> decoded(size);
    BWT_decode(&decoded[0], encoded, size, primaryIndex);
    return decoded;
}

// Embed index -versions

const std::vector<byte>
    BWT_encode_embedindex(const byte* input, size_t size)
{
    std::vector<byte> encoded ( size + 4);
    size_t primaryIndex = BWT_encode(&encoded[4], input, size);
    W32(&encoded[0], primaryIndex);
    return encoded;
}

const std::vector<byte>
    BWT_decode_embedindex(const byte* encoded, size_t size)
{
    if(size < 4) return std::vector<byte> ();
    std::vector<byte> decoded(size - 4);
    BWT_decode(&decoded[0], encoded+4, size-4, R32(&encoded[0]));
    return decoded;
}

/////////////// Vector versions. 

const std::vector<byte>
    BWT_encode(const std::vector<byte>& input, size_t& primary_index)
{
    return BWT_encode(&input[0], input.size(), primary_index);
}

const std::vector<byte>
    BWT_encode_embedindex(const std::vector<byte>& input)
{
    return BWT_encode_embedindex(&input[0], input.size());
}

const std::vector<byte>
    BWT_decode(const std::vector<byte>& input, const size_t primary_index)
{
    return BWT_decode(&input[0], input.size(), primary_index);
}

const std::vector<byte>
    BWT_decode_embedindex(const std::vector<byte>& input)
{
    return BWT_decode_embedindex(&input[0], input.size());
}
