#include "lzma/C/7zip/Compress/LZMA_Alone/LzmaRam.h"
#include "lzma/C/Common/MyInitGuid.h"
#include "lzma/C/7zip/ICoder.h"

#include "lzma.hh"

const std::vector<unsigned char> LZMACompress(const std::vector<unsigned char>& buf)
{
    if(buf.empty()) return buf;
    
    std::vector<unsigned char> result(buf.size() + 1);
    
    const unsigned dict_size = buf.size();
    
    for(;;)
    {
        size_t result_size;
        
        int res = LzmaRamEncode((const Byte*)&buf[0], buf.size(),
                                (Byte*)&result[0], result.size(),
                                &result_size,
                                dict_size,
                                SZ_FILTER_NO);

        if(res == 0)
        {
            result.resize(result_size);
            break;
        }
        //fprintf(stderr, "LZMA error %d, trying a bigger buffer\n", res);
        result.resize(result.size() * 2);
    }
    
    result.erase(result.begin(), result.begin()+1);
    
    return result;
}

#include "../LzmaDecode.h"
#include "../LzmaDecode.c"

const std::vector<unsigned char> LZMADeCompress
    (const std::vector<unsigned char>& buf)
{
    if(buf.size() <= 5+8) return std::vector<unsigned char> ();
    
    /* FIXME: not endianess-safe */
    uint_least64_t out_sizemax = *(const uint_least64_t*)&buf[5];
    
    std::vector<unsigned char> result(out_sizemax);
    
    CLzmaDecoderState state;
    LzmaDecodeProperties(&state.Properties, &buf[0], LZMA_PROPERTIES_SIZE);
    state.Probs = new CProb[LzmaGetNumProbs(&state.Properties)];
    
    SizeT in_done;
    SizeT out_done;
    LzmaDecode(&state, &buf[13], buf.size()-13, &in_done,
               &result[0], result.size(), &out_done);
    
    delete[] state.Probs;
    
    result.resize(out_done);
    return result;
}

