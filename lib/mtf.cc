#include <cstring>

#include "mtf.hh"

typedef unsigned char byte;

const std::vector<byte>
    MTF_encode(const unsigned char* const input, const size_t size)
{
    std::vector<byte> encoded(size);

    byte indexList[256];
    for(unsigned i = 0; i < 256; ++i) indexList[i] = i;

    for(size_t i = 0; i < size; ++i)
    {
        const byte value = input[i];

        byte indexListValue = indexList[0];
        for(unsigned j = 0; true;)
        {
            if(indexListValue == value)
            {
                encoded[i] = j;
                indexList[0] = indexListValue;
                break;
            }

            ++j;
            byte tmp = indexList[j];
            indexList[j] = indexListValue;
            indexListValue = tmp;
        }
    }
    
    return encoded;
}

const std::vector<byte>
    MTF_decode(const unsigned char* const encoded, const size_t size)
{
    std::vector<byte> decoded(size);

    byte indexList[256];
    for(unsigned i = 0; i < 256; ++i) indexList[i] = i;

    for(size_t i = 0; i < size; ++i)
    {
        const byte value = encoded[i];
        const byte indexListValue = indexList[value];
        decoded[i] = indexListValue;
        
        std::memmove(&indexList[1], &indexList[0], value);
        /*
        for(unsigned j = value; j > 0; --j)
            indexList[j] = indexList[j-1];
        */
        indexList[0] = indexListValue;
    }
    
    return decoded;
}
