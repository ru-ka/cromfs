#include "endian.hh" /* For R64 */

#include "lzma/CPP/Common/MyWindows.h"
#include "lzma/CPP/Common/MyInitGuid.h"
#include "lzma/CPP/7zip/Compress/LZMA/LZMAEncoder.h"

#include "lzma.hh"
#include "threadfun.hh" // for ForceSwitchThread

#include <vector>
#include <algorithm>
#include <string>

#include <stdint.h>

static OLECHAR LZMA_MatchFinder_full[] = L"BT4";
static OLECHAR LZMA_MatchFinder_quick[] = L"HC4";

int LZMA_verbose = 0;

// -fb
unsigned LZMA_NumFastBytes = 273;
/*from lzma.txt:
          Set number of fast bytes - [5, 273], default: 273
          Usually big number gives a little bit better compression ratio
          and slower compression process.
  from anonymous:
This one is hard to explain... To my knowledge (please correct me if I
am wrong), this refers to the optimal parsing algorithm. The algorithm
tries many different combinations of matches to find the best one. If a
match is found that is over the fb value, then it will not be optimised,
and will just be used straight.
This speeds up corner cases such as pic.   
*/

/* apparently, 0 and 1 are valid values. 0 = fast mode */
unsigned LZMA_AlgorithmNo  = 1;

unsigned LZMA_MatchFinderCycles = 0; // default: 0

// -pb
unsigned LZMA_PosStateBits = 0; // default: 2, range: 0..4
/*from lzma.txt:
          pb switch is intended for periodical data 
          when period is equal 2^N.
*/


// -lp
unsigned LZMA_LiteralPosStateBits = 0; // default: 0, range: 0..4
/*from lzma.txt:
          lp switch is intended for periodical data when period is 
          equal 2^N. For example, for 32-bit (4 bytes) 
          periodical data you can use lp=2.
          Often it's better to set lc0, if you change lp switch.
*/

// -lc
unsigned LZMA_LiteralContextBits = 1; // default: 3, range: 0..8
/*from lzma.txt:
          Sometimes lc=4 gives gain for big files.
  from anonymous:
The context for the literal coder is 2^(lc) long. The longer it is, the
better the statistics, but also the slower it adapts. A tradeoff, which
is why 3 or 4 is reccommended.
*/

/*

Discoveries:

 INODES:
    Best LZMA for raw_inotab_inode(40->48): pb0 lp0 lc0
    Best LZMA for raw_root_inode(28->32): pb0 lp0 lc0

    Start LZMA(rootdir, 736 bytes)
    Yay result with pb0 lp0 lc0: 218
    Yay result with pb0 lp0 lc1: 217
    Best LZMA for rootdir(736->217): pb0 lp0 lc1

    Start LZMA(inotab, 379112 bytes)
    Yay result with pb0 lp0 lc0: 24504
    Best LZMA for inotab(379112->24504): pb0 lp0 lc0
 
 BLKTAB:
    Best LZMA for raw_blktab(10068->2940): pb2 lp2 lc0

    ---with fastbytes=128---
    Start LZMA(blktab, 12536608 bytes)
    Yay result with pb0 lp0 lc0: 1386141
    Yay result with pb0 lp1 lc0: 1308137
    Yay result with pb0 lp2 lc0: 1305403
    Yay result with pb0 lp3 lc0: 1303072
    Yay result with pb1 lp1 lc0: 1238990
    Yay result with pb1 lp2 lc0: 1227973
    Yay result with pb1 lp3 lc0: 1221205
    Yay result with pb2 lp1 lc0: 1197035
    Yay result with pb2 lp2 lc0: 1188979
    Yay result with pb2 lp3 lc0: 1184531
    Yay result with pb3 lp1 lc0: 1183866
    Yay result with pb3 lp2 lc0: 1172994
    Yay result with pb3 lp3 lc0: 1169048
    Best LZMA for blktab(12536608->1169048): pb3 lp3 lc0
    
    It seems, lc=0 and pb=lp=N is a wise choice,
    where N is 2 for packed blktab and 3 for unpacked.

 FBLOCKS:
    For SPC sound+code data, the best results
     are between:
      pb0 lp0 lc0 (10%)
      pb0 lp0 lc1 (90%)
     For inotab, these were observed:
      pb1 lp0 lc1
      pb2 lp0 lc0
      pb1 lp1 lc0
      pb3 lp1 lc0
      pb1 lp2 lc0
      pb2 lp1 lc0
      
    For C source code data, the best results 
     are between:
      pb1 lp0 lc3 (10%)
      pb0 lp0 lc3 (90%)
     Occasionally:
      pb0 lp1 lc0
      pb0 lp0 lc3 (mostly)
      pb0 lp0 lc2
      pb0 lp0 lc4
     Occasionally 2:
      pb0 lp0 lc8
      pb0 lp0 lc4

    BUT:
    Best LZMA for fblock(204944->192060): pb0 lp4 lc8 -- surprise! (INOTAB PROBABLY)

*/

static UInt32 SelectDictionarySizeFor(unsigned datasize)
{
   #if 1
    if(datasize >= (1 << 30U)) return 1 << 30U;
    return datasize;
   #else
#ifdef __GNUC__
    /* gnu c can optimize this switch statement into a fast binary
     * search, but it cannot do so for the list of the if statements.
     */
    switch(datasize)
    {
        case 0 ... 512 : return 512;
        case 513 ... 1024: return 2048;
        case 1025 ... 4096: return 8192;
        case 4097 ... 16384: return 32768;
        case 16385 ... 65536: return 528288;
        case 65537 ... 528288: return 1048576*4;
        case 528289 ... 786432: return 1048576*16;
        default: return 1048576*32;
    }
#else
    if(datasize <= 512) return 512;
    if(datasize <= 1024) return 1024;
    if(datasize <= 4096) return 4096;
    if(datasize <= 16384) return 32768; 
    if(datasize <= 65536) return 528288;
    if(datasize <= 528288) return 1048576*4;
    if(datasize <= 786432) reutrn 1048576*16;
    return 32*1048576;
#endif
   #endif
}


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

const std::vector<unsigned char> LZMACompress(const std::vector<unsigned char>& buf,
    unsigned pb,
    unsigned lp,
    unsigned lc)
{
    return LZMACompress(buf, pb,lp,lc,
        SelectDictionarySizeFor(buf.size()));
}

const std::vector<unsigned char> LZMACompress(const std::vector<unsigned char>& buf,
    unsigned pb,
    unsigned lp,
    unsigned lc,
    unsigned dictionarysize)
{
    if(buf.empty()) return buf;
    
    NCompress::NLZMA::CEncoder *encoderSpec = new NCompress::NLZMA::CEncoder;
    CMyComPtr<ICompressCoder> encoder = encoderSpec;
    const PROPID propIDs[] = 
    {
        NCoderPropID::kAlgorithm,
        NCoderPropID::kDictionarySize,  
        NCoderPropID::kNumFastBytes,
        NCoderPropID::kMatchFinderCycles,
        NCoderPropID::kPosStateBits,
        NCoderPropID::kLitPosBits,
        NCoderPropID::kLitContextBits,
        NCoderPropID::kMatchFinder
        /*
    Other properties to consider:
        kMatchFinder (VT_BSTR)
        kMatchFinderCycles (VT_UI4)
        kEndMarker (VT_BOOL) --apparently, this is false by default, so no need to adjust
        */
    };
    const unsigned kNumProps = sizeof(propIDs) / sizeof(propIDs[0]);
    PROPVARIANT properties[kNumProps];
    properties[0].vt = VT_UI4; properties[0].ulVal = (UInt32)LZMA_AlgorithmNo;
    properties[1].vt = VT_UI4; properties[1].ulVal = (UInt32)dictionarysize;
    properties[2].vt = VT_UI4; properties[2].ulVal = (UInt32)LZMA_NumFastBytes;
    properties[3].vt = VT_UI4; properties[3].ulVal = (UInt32)LZMA_MatchFinderCycles;
    properties[4].vt = VT_UI4; properties[4].ulVal = (UInt32)pb;
    properties[5].vt = VT_UI4; properties[5].ulVal = (UInt32)lp;
    properties[6].vt = VT_UI4; properties[6].ulVal = (UInt32)lc;
    properties[7].vt = VT_BSTR; properties[7].bstrVal =
        (LZMA_AlgorithmNo ? LZMA_MatchFinder_full : LZMA_MatchFinder_quick);

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

const std::vector<unsigned char> LZMACompress(const std::vector<unsigned char>& buf)
{
    return LZMACompress(buf,
        LZMA_PosStateBits,
        LZMA_LiteralPosStateBits,
        LZMA_LiteralContextBits);
}

#undef RC_NORMALIZE

#include "LzmaDecode.h"
#include "LzmaDecode.c"

const std::vector<unsigned char> LZMADeCompress
    (const std::vector<unsigned char>& buf, bool& ok)
{
    if(buf.size() <= 5+8) 
    {
    clearly_not_ok:
        ok = false;
        return std::vector<unsigned char> ();
    }
    
    uint_least64_t out_sizemax = R64(&buf[5]);
    
    /*if(out_sizemax >= (size_t)~0ULL)
    {
        // cannot even allocate a vector this large.
        goto clearly_not_ok;
    }*/
    
    std::vector<unsigned char> result(out_sizemax);
    
    CLzmaDecoderState state;
    LzmaDecodeProperties(&state.Properties, &buf[0], LZMA_PROPERTIES_SIZE);
    state.Probs = new CProb[LzmaGetNumProbs(&state.Properties)];
    
    SizeT in_done;
    SizeT out_done;
    int res = LzmaDecode(&state, &buf[13], buf.size()-13, &in_done,
                         &result[0], result.size(), &out_done);
    
    /*
    fprintf(stderr, "res=%d, in_done=%d (buf=%d), out_done=%d (max=%d)\n",
        res, (int)in_done, (int)buf.size(),
             (int)out_done, (int)out_sizemax);
    */
    
    ok = out_done == out_sizemax
      && in_done+5+8 == buf.size()
      && res == LZMA_RESULT_OK;
    
    delete[] state.Probs;
    
    result.resize(out_done);
    return result;
}

const std::vector<unsigned char> LZMADeCompress
    (const std::vector<unsigned char>& buf)
{
    bool ok_unused;
    return LZMADeCompress(buf, ok_unused);
}

#if 0
#include <stdio.h>
int main(void)
{
    char Buf[2048*2048];
    int s = fread(Buf,1,sizeof(Buf),stdin);
    std::vector<unsigned char> result = LZMADeCompress(std::vector<unsigned char>(Buf,Buf+s));
    fwrite(&result[0],1,result.size(),stdout);
}
#endif

const std::vector<unsigned char> LZMACompressHeavy(const std::vector<unsigned char>& buf,
    const char* why)
{
    std::vector<unsigned char> bestresult;
    char best[512];
    bool first = true;
    if(LZMA_verbose >= 1)
    {
        fprintf(stderr, "Start LZMA(%s, %u bytes)\n", why, (unsigned)buf.size());
        fflush(stderr);
    }
    
    unsigned minresultsize=0, maxresultsize=0;
    unsigned sizemap[5][5][9] = {{{0}}};
    
    bool use_small_dict = false;
    
  #pragma omp parallel for
    for(unsigned compress_mode = 0; compress_mode < (5*5*9); ++compress_mode)
    {
        const unsigned pb = compress_mode % 5;
        const unsigned lp = (compress_mode / 5) % 5;
        const unsigned lc = (compress_mode / 5 / 5) % 9;
        
        std::vector<unsigned char>
            result = use_small_dict
                ? LZMACompress(buf,pb,lp,lc, 4096)
                : LZMACompress(buf,pb,lp,lc);
        
      #pragma omp critical (lzmacompressheavy_updatestatistics)
       {
        sizemap[pb][lp][lc] = result.size();
        
        if(first || result.size() < minresultsize) minresultsize = result.size();
        if(first || result.size() > maxresultsize) maxresultsize = result.size();
        if(first || result.size() < bestresult.size())
        {
            sprintf(best, "pb%u lp%u lc%u",
                pb,lp,lc);
            if(LZMA_verbose >= 1)
                fprintf(stderr, "Yay result with %s: %u\n", best, (unsigned)result.size());
            bestresult.swap(result);
            first = false;
        }
        else
        {
            char tmp[512];
            sprintf(tmp, "pb%u lp%u lc%u",
                pb,lp,lc);
            if(LZMA_verbose >= 2)
                fprintf(stderr, "Blaa result with %s: %u\n", tmp, (unsigned)result.size());
        }
        if(LZMA_verbose >= 2)
        {
            fprintf(stderr, "%*s\n", (5 * (4+9+2)), "");
            /* Visualize the size map: */
            std::string lines[6] = {};
            for(unsigned pbt = 0; pbt <= 4; ++pbt)
            {
                char buf[64]; sprintf(buf, "pb%u:%11s", pbt,"");
                lines[0] += buf;
                
                for(unsigned lpt = 0; lpt <= 4; ++lpt)
                {
                    char buf[64]; sprintf(buf, "lp%u:", lpt);
                    std::string line;
                    line += buf;
                    for(unsigned lct = 0; lct <= 8; ++lct)
                    {
                        unsigned s = sizemap[pbt][lpt][lct];
                        char c;
                        if(!s) c = '.';
                        else c = 'a' + ('z'-'a'+1)
                                     * (s - minresultsize)
                                     / (maxresultsize-minresultsize+1);
                        line += c;
                    }
                    lines[1 + lpt] += line + "  ";
                }
            }
            for(unsigned a=0; a<6; ++a) fprintf(stderr, "%s\n", lines[a].c_str());
            fprintf(stderr, "\33[%uA", 7);
        }
       }
    }
    if(LZMA_verbose >= 2)
        fprintf(stderr, "\n\n\n\n\n\n\n\n");
    
    if(LZMA_verbose >= 1)
    {
        fprintf(stderr, "Best LZMA for %s(%u->%u): %s\n",
            why,
            (unsigned)buf.size(),
            (unsigned)bestresult.size(),
            best);
    }
    fflush(stderr);
    return bestresult;
}

/*

The LZMA compression power is controlled by these parameters:
  Dictionary size (we use the maximum)
  Compression algorithm (we use BT4, the heaviest available)
  Number of fast bytes (we use the maximum)
  pb (0..4), lp (0..4) and lc (0..8) -- the effect of these depends on data.

Since the only parameters whose effect depends on the data to be compressed
are the three (pb, lp, lc), the "auto" and "full" compression algorithms
only try to find the optimal values for those.

The "auto" LZMA compression algorithm is based on these two assumptions:
  - It is possible to find the best value for each component (pb, lp, lc)
    by individually testing the most effective one of them while keeping
    the others static.
    I.e.,    step 1: pb=<find best>, lp=0, lc=0
             step 2: pb=<use result>, lp=<find best>, lc=0
             step 3: pb=<use result>, lp=<use result>, lc=<find best>
             final: pb=<use result>, lp=<use result>, lc=<use result>
  - That the effect of each of these components forms a parabolic function
    that has a starting point, ending point, and possibly a mountain or a
    valley somewhere in the middle, but never a valley _and_ a mountain, nor
    two valleys nor two mountains.
These assumptions are not always true, but it gets very close to the optimum.

The ParabolicFinder class below finds the lowest point in a parabolic curve
with a small number of tests, determining the shape of the curve by sampling
a few cue values as needed.

The algorithm is like this:
  Never check any value more than once.
  Check the first two values.
  If they differ, then check the last in sequence.
    If not, then check everything in sequential order.
  If the first two values and the last form an ascending sequence, accept the first value.
    If they form a descending sequence, start Focus Mode
    such that the focus lower limit is index 2 and upper
    limit is the second last. Then check the second last.
      If they don't, then check the third value of sequence,
      and everything else in sequential order.
  If in Focus Mode, check if being in the lower or upper end of the focus.
    If in upper end, check if the current value is bigger than the next one.
      If it is, end the process, because the smallest value has already been found.
        If not, next check the value at focus_low, and increase focus_low.
    If in lower end, check if the current value is bigger than the previous one.
      If it is, end the process, because the smallest value has already been found.
        If not, next check the value at focus_high, and decrease focus_high.

For any sample space, it generally does 3 tests, but if it detects a curve
forming a valley, it may do more.

Note that ParabolicFinder does not _indicate_ the lowest value. It leaves that
to the caller. It just stops searching when it thinks that no lower value will
be found.

Note: The effect of pb, lp and lc depend also on the dictionary size setting
and compression algorithm. You cannot estimate the optimal value for those
parameters reliably using different compression settings than in the actual case.

*/
class ParabolicFinder
{
public:
    enum QueryState      { Unknown, Pending, Done };
    enum InstructionType { HereYouGo, WaitingResults, End };
public:
    ParabolicFinder(unsigned Start, unsigned End)
        : begin(Start),
          results(End-Start+1, 0),
          state  (End-Start+1, Unknown),
          LeftRightSwap(false)
    {
    }
    
    InstructionType GetNextInstruction(unsigned& attempt)
    {
      InstructionType result = End;
      
      const int Last  = begin + results.size()-1;
      
      #define RetIns(n) do{ result = (n); goto DoneCrit; }while(0)
      #define RetVal(n) do{ state[attempt = (n)] = Pending; RetIns(HereYouGo); }while(0)
      
      #pragma omp critical(LZMA_ParabolicFinderState)
      {
        /*
        fprintf(stderr, "NextInstruction...");
        for(unsigned a=0; a<state.size(); ++a)
            fprintf(stderr, " %u=%s", a,
                state[a]==Unknown?"??"
               :state[a]==Done?"Ok"
               :"..");
        fprintf(stderr, "\n");*/
        
        if(CountUnknown() == 0)
        {
            // No unassigned slots remain. Don't need more workers.
            RetIns(End);
        }

        if(1) // scope for local variables
        {
            // Alternate which side to do next if both are available.
            bool LeftSideFirst = LeftRightSwap ^= 1;
            
            // Check left side descend type
            int LeftSideNext = -1; bool LeftSideDoable = false;
            for(int c=0; c<=Last; ++c)
                switch(state[c])
                {
                    case Unknown: LeftSideNext = c; LeftSideDoable = true; goto ExitLeftSideFor;
                    case Pending: LeftSideNext = c; LeftSideDoable = false; goto ExitLeftSideFor;
                    case Done:
                        if(c == 0) continue;
                        if(results[c] > results[c-1])
                        {
                            // Left side stopped descending.
                            if(state[Last] != Unknown) RetIns(End);
                            goto ExitLeftSideFor;
                        }
                        else if(results[c] == results[c-1])
                            LeftSideFirst = true;
                }
        ExitLeftSideFor: ;
            
            // Check right side descend type
            int RightSideNext = -1; bool RightSideDoable = false;
            for(int c=Last; c>=0; --c)
                switch(state[c])
                {
                    case Unknown: RightSideNext = c; RightSideDoable = true; goto ExitRightSideFor;
                    case Pending: RightSideNext = c; RightSideDoable = false; goto ExitRightSideFor;
                    case Done:
                        if(c == Last) continue;
                        if(results[c] > results[c+1])
                        {
                            // Right side stopped descending.
                            if(state[0] != Unknown) RetIns(End);
                            goto ExitRightSideFor;
                        }
                        else if(results[c] == results[c+1])
                            LeftSideFirst = false;
                }
        ExitRightSideFor: ;
        
            if(!LeftSideFirst)
                 { std::swap(LeftSideDoable, RightSideDoable);
                   std::swap(LeftSideNext,   RightSideNext); }
            
            if(LeftSideDoable) RetVal(LeftSideNext);
            if(RightSideDoable) RetVal(RightSideNext);
            
            // If we have excess threads and work to do, give them something
            if(CountHandled() > 2) if(LeftSideNext >= 0) RetVal(LeftSideNext);
            if(CountHandled() > 3) if(RightSideNext >= 0) RetVal(RightSideNext);
            
            RetIns(WaitingResults);
        }
        
      DoneCrit: ;
      }
      return result;
    }
    
    void GotResult(unsigned attempt, unsigned value)
    {
      #pragma omp critical(LZMA_ParabolicFinderState)
      {
        results[attempt] = value;
        state[attempt]   = Done;
      }
    }

private:
    unsigned CountUnknown() const
    {
        unsigned result=0;
        for(size_t a=0, b=state.size(); a<b; ++a)
            if(state[a] == Unknown) ++result;
        return result;
    }
    unsigned CountHandled() const
    {
        return state.size() - CountUnknown();
    }
private:
    unsigned begin;
    std::vector<unsigned>   results;
    std::vector<QueryState> state;
    bool LeftRightSwap;
};

static void LZMACompressAutoHelper(
    const std::vector<unsigned char>& buf, bool use_small_dict,
    const char* why,
    unsigned& pb, unsigned& lp, unsigned& lc,
    unsigned& which_iterate, ParabolicFinder& finder,
    bool&first, std::vector<unsigned char>& bestresult)
{
    for(;;)
    {
        unsigned t=0;
        switch(finder.GetNextInstruction(t))
        {
            case ParabolicFinder::End:
                return;
            case ParabolicFinder::HereYouGo:
                break;
            case ParabolicFinder::WaitingResults:
                ForceSwitchThread();
                continue;
        }
        
        const unsigned try_pb = &which_iterate == &pb ? t : pb;
        const unsigned try_lp = &which_iterate == &lp ? t : lp;
        const unsigned try_lc = &which_iterate == &lc ? t : lc;
        
        if(LZMA_verbose >= 2)
            fprintf(stderr, "%s:Trying pb%u lp%u lc%u\n",
                why,try_pb,try_lp,try_lc);
        
        std::vector<unsigned char> result = use_small_dict
            ? LZMACompress(buf,try_pb,try_lp,try_lc, 65536)
            : LZMACompress(buf,try_pb,try_lp,try_lc);

        if(LZMA_verbose >= 2)
            fprintf(stderr, "%s:       pb%u lp%u lc%u -> %u\n",
                why,try_pb,try_lp,try_lc, (unsigned)result.size());
        
        finder.GotResult(t, result.size());
        
      #pragma omp critical(LZMA_Auto_UpdateStats)
      {
        if(first || result.size() <= bestresult.size())
        {
            first    = false;
            bestresult.swap(result);
            which_iterate = t;
        }
      }
    }
}


const std::vector<unsigned char> LZMACompressAuto(const std::vector<unsigned char>& buf,
    const char* why)
{
    if(LZMA_verbose >= 1)
    {
        fprintf(stderr, "Start LZMA(%s, %u bytes)\n", why, (unsigned)buf.size());
        fflush(stderr);
    }
    
    unsigned backup_algorithm = LZMA_AlgorithmNo;
    
    bool use_small_dict = false;//buf.size() >= 1048576;
    
    if(use_small_dict) LZMA_AlgorithmNo = 0;
    
    unsigned pb=0, lp=0, lc=0;

    std::vector<unsigned char> bestresult;
  
  {
    ParabolicFinder pb_finder(0,4);
    ParabolicFinder lp_finder(0,4);
    ParabolicFinder lc_finder(0,8);
    bool first=true;
  #pragma omp parallel
   {
    /* Using parallelism here. However, we need barriers after
     * each step, because the comparisons are made based on the
     * result size, and if the pb/lp/lc values other than the
     * one being focused change, it won't work. Only one parameter
     * must change in the loop.
     */
    
    /* step 1: find best value in pb axis */
    LZMACompressAutoHelper(buf,use_small_dict,why,
        pb, lp, lc,
        pb, pb_finder, first, bestresult);

    #pragma omp barrier

    #pragma omp single
    lp_finder.GotResult(lp, bestresult.size());
    
    /* step 2: find best value in lp axis */
    LZMACompressAutoHelper(buf,use_small_dict,why,
        pb, lp, lc,
        lp, lp_finder, first, bestresult);

    #pragma omp barrier

    #pragma omp single
    lc_finder.GotResult(lc, bestresult.size());
    
    /* step 3: find best value in lc axis */
    LZMACompressAutoHelper(buf,use_small_dict,why,
        pb, lp, lc,
        lc, lc_finder, first, bestresult);
   }
  }
  
    if(use_small_dict || LZMA_AlgorithmNo != backup_algorithm)
    {
        LZMA_AlgorithmNo = backup_algorithm;
        bestresult = LZMACompress(buf, pb,lp,lc);
    }
    
    if(LZMA_verbose >= 1)
    {
        fprintf(stderr, "Best LZMA for %s(%u->%u): pb%u lp%u lc%u\n",
            why,
            (unsigned)buf.size(),
            (unsigned)bestresult.size(),
            pb,lp,lc);
    }
    fflush(stderr);
    
    return bestresult;
}

const std::vector<unsigned char>
    DoLZMACompress(int HeavyLevel,
        const std::vector<unsigned char>& data, const char* why)
{
    if(HeavyLevel >= 2) return LZMACompressHeavy(data, why);
    if(HeavyLevel >= 1) return LZMACompressAuto(data, why);
    return LZMACompress(data);
}