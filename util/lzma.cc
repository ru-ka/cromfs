#include "lzma/C/Common/MyInitGuid.h"
#include "lzma/C/7zip/Compress/LZMA/LZMAEncoder.h"

#include "lzma.hh"

#include <vector>
#include <algorithm>

class CInStreamRam: public ISequentialInStream, public CMyUnknownImp
{
    const std::vector<unsigned char>& input;
    size_t Pos;
public:
    MY_UNKNOWN_IMP
  
    CInStreamRam(const std::vector<unsigned char>& buf) : input(buf), Pos(0)
    {
    }
    virtual ~CInStreamRam() {}
  
    STDMETHOD(Read)(void *data, UInt32 size, UInt32 *processedSize);
};

STDMETHODIMP CInStreamRam::Read(void *data, UInt32 size, UInt32 *processedSize)
{
    UInt32 remain = input.size() - Pos;
    if (size > remain) size = remain;
  
    std::memcpy(data, &input[Pos], size);
    Pos += size;
    
    if(processedSize != NULL) *processedSize = size;
    
    return S_OK;
}

class COutStreamRam: public ISequentialOutStream, public CMyUnknownImp
{
    std::vector<Byte> result;
    size_t Pos;
public:
    MY_UNKNOWN_IMP
    
    COutStreamRam(): result(), Pos(0) { }
    virtual ~COutStreamRam() { }
    
    void Reserve(unsigned n) { result.reserve(n); }
    const std::vector<Byte>& Get() const { return result; }
  
    HRESULT WriteByte(Byte b)
    {
        if(Pos >= result.size()) result.resize(Pos+1);
        result[Pos++] = b;
        return S_OK;
    }
  
    STDMETHOD(Write)(const void *data, UInt32 size, UInt32 *processedSize);
};
STDMETHODIMP COutStreamRam::Write(const void *data, UInt32 size, UInt32 *processedSize)
{
    if(Pos+size > result.size()) result.resize(Pos+size);
    
    std::memcpy(&result[Pos], data, size);
    if(processedSize != NULL) *processedSize = size;
    Pos += size;
    return S_OK;
}

const std::vector<unsigned char> LZMACompress(const std::vector<unsigned char>& buf)
{
    if(buf.empty()) return buf;
    
    const UInt32 dictionarysize = buf.size();
    
    //const UInt32 dictionarysize = std::max((unsigned long)buf.size(), 32*1048576UL);
    
    NCompress::NLZMA::CEncoder *encoderSpec = new NCompress::NLZMA::CEncoder;
    CMyComPtr<ICompressCoder> encoder = encoderSpec;
    const PROPID propIDs[] = 
    {
        NCoderPropID::kAlgorithm,
        NCoderPropID::kDictionarySize,  
        NCoderPropID::kNumFastBytes,
    };
    const unsigned kNumProps = sizeof(propIDs) / sizeof(propIDs[0]);
    PROPVARIANT properties[kNumProps];
    properties[0].vt = VT_UI4; properties[0].ulVal = (UInt32)2;
    properties[1].vt = VT_UI4; properties[1].ulVal = (UInt32)dictionarysize;
    properties[2].vt = VT_UI4; properties[2].ulVal = (UInt32)64;

    if (encoderSpec->SetCoderProperties(propIDs, properties, kNumProps) != S_OK)
    {
    Error:
        return std::vector<unsigned char> ();
    }
    
    COutStreamRam *const outStreamSpec = new COutStreamRam;
    CMyComPtr<ISequentialOutStream> outStream = outStreamSpec;
    CInStreamRam *const inStreamSpec = new CInStreamRam(buf);
    CMyComPtr<ISequentialInStream> inStream = inStreamSpec;
    
    outStreamSpec->Reserve(buf.size());

    if (encoderSpec->WriteCoderProperties(outStream) != S_OK) goto Error;
    
    for (unsigned i = 0; i < 8; i++)
    {
        UInt64 t = (UInt64)buf.size();
        outStreamSpec->WriteByte((Byte)((t) >> (8 * i)));
    }

    HRESULT lzmaResult = encoder->Code(inStream, outStream, 0, 0, 0);
    if (lzmaResult != S_OK) goto Error;
    
    return outStreamSpec->Get();
}

#undef RC_NORMALIZE

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
