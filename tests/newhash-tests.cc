#ifndef __STDC_CONSTANT_MACROS
#define __STDC_CONSTANT_MACROS /* for UINT16_C etc */
#endif

#include <stdint.h>
#include <algorithm>
#include <stdio.h>
#include <string.h>
#include <vector>

#include "hashtests/rand.c"
#undef mix

#include <emmintrin.h>

typedef uint_least32_t c32;
typedef uint_least64_t c64;
typedef uint_least8_t byte;
typedef unsigned int c128 __attribute__((mode(TI)));
//typedef c64 c64_2 __attribute__ ((__vector_size__ (16))); // two c64s in a vector
typedef __v2di c64_2;
typedef c32 RetT;

#ifndef UINT32_C
# define UINT32_C(x) x##ul
#endif
#ifndef UINT64_C
# define UINT64_C(x) x##ull
#endif

template<typename T>
static T sr(T v, int n) { if(n < 0) return v << n; return v >> n; }
template<typename T>
static T sl(T v, int n) { if(n < 0) return v >> n; return v << n; }
template<typename T>
static T rol(T v, int n) { return (v<<n) | (v>>( sizeof(T)*8 - n)); }


int mix32const[3*3] = { 5, 15, 13, 11,  7,  5,  3, 13, 15};

struct mixer32
{
    static void mix(c32& a, c32& b, c32& c)
    {
  a=(a-b-c) ^ sr(c,mix32const[0]);
  b=(b-c-a) ^ sl(a,mix32const[1]);
  c=(c-a-b) ^ sr(b,mix32const[2]);

  a=(a-b-c) ^ sr(c,mix32const[3]);
  b=(b-c-a) ^ sl(a,mix32const[4]);
  c=(c-a-b) ^ sr(b,mix32const[5]);

  a=(a-b-c) ^ sr(c,mix32const[6]);
  b=(b-c-a) ^ sl(a,mix32const[7]);
  c=(c-a-b) ^ sr(b,mix32const[8]);
    }

    static RetT test(byte data[96])
    {
        c32 a(UINT32_C(0x9e3779b9));
        c32 b(a), c(0);
        a += *(c32*)&data[0];
        b += *(c32*)&data[4];
        c += *(c32*)&data[8];
        mix(a,b,c);
        a += *(c32*)&data[12];
        b += *(c32*)&data[16];
        c += *(c32*)&data[20];
        mix(a,b,c);
        a += *(c32*)&data[24];
        b += *(c32*)&data[28];
        c += *(c32*)&data[32];
        mix(a,b,c);
        a += *(c32*)&data[36];
        b += *(c32*)&data[40];
        c += *(c32*)&data[44];
        mix(a,b,c);
        a += *(c32*)&data[48];
        b += *(c32*)&data[52];
        c += *(c32*)&data[56];
        mix(a,b,c);
        a += *(c32*)&data[60];
        b += *(c32*)&data[64];
        c += *(c32*)&data[68];
        mix(a,b,c);
        a += *(c32*)&data[72];
        b += *(c32*)&data[76];
        c += *(c32*)&data[80];
        mix(a,b,c);
        a += *(c32*)&data[84];
        b += *(c32*)&data[88];
        c += *(c32*)&data[92];
        mix(a,b,c);
        return c;
    }
};


int mix32zconst[2*3+4] = //{ 4,6,8,16,19,4, 14,11,25,16,4,14,24 };
                         { 30,  8,  1,  5, 14,  8, 27, 22,  4, 11 };

struct mixer32z
{
    static inline void mix(c32& a, c32& b, c32& c)
    {
  a=(a-c) ^ rol(c,mix32zconst[0]); c += b;
  b=(b-a) ^ rol(a,mix32zconst[1]); a += c;
  c=(c-b) ^ rol(b,mix32zconst[2]); b += a;
  a=(a-c) ^ rol(c,mix32zconst[3]); c += b;
  b=(b-a) ^ rol(a,mix32zconst[4]); a += c;
  c=(c-b) ^ rol(b,mix32zconst[5]); b += a;
    }
    static inline void final(c32& a, c32& b, c32& c)
    {
  c=(c^b) - rol(b,mix32zconst[6]);
  a=(a^c) - rol(c,mix32zconst[7]);
  b=(b^a) - rol(a,mix32zconst[8]);
  c=(c^b) - rol(b,mix32zconst[9]);/*
  a=(a^c) - rol(c,mix32zconst[10]);
  b=(b^a) - rol(a,mix32zconst[11]);
  c=(c^b) - rol(b,mix32zconst[12]);*/
    }

    static RetT test(byte data[96])
    {
        c32 a(UINT32_C(0x9e3779b9) + 96);
        c32 b(a), c(a);
        a += *(c32*)&data[0];
        b += *(c32*)&data[4];
        c += *(c32*)&data[8];
        mix(a,b,c);
        a += *(c32*)&data[12];
        b += *(c32*)&data[16];
        c += *(c32*)&data[20];
        mix(a,b,c);
        a += *(c32*)&data[24];
        b += *(c32*)&data[28];
        c += *(c32*)&data[32];
        mix(a,b,c);
        a += *(c32*)&data[36];
        b += *(c32*)&data[40];
        c += *(c32*)&data[44];
        mix(a,b,c);
        a += *(c32*)&data[48];
        b += *(c32*)&data[52];
        c += *(c32*)&data[56];
        mix(a,b,c);
        a += *(c32*)&data[60];
        b += *(c32*)&data[64];
        c += *(c32*)&data[68];
        mix(a,b,c);
        a += *(c32*)&data[72];
        b += *(c32*)&data[76];
        c += *(c32*)&data[80];
        mix(a,b,c);
        a += *(c32*)&data[84];
        b += *(c32*)&data[88];
        c += *(c32*)&data[92];
        mix(a,b,c);
        final(a,b,c);
        return c;
    }
};

int mix64zconst[2*3+4] = {  41, 58, 16, 34, 19,  7,  4, 30, 50, 59};

struct mixer64z
{
    static inline void mix(c64& a, c64& b, c64& c)
    {
  a=(a-c) ^ rol(c,mix64zconst[0]); c += b;
  b=(b-a) ^ rol(a,mix64zconst[1]); a += c;
  c=(c-b) ^ rol(b,mix64zconst[2]); b += a;
  a=(a-c) ^ rol(c,mix64zconst[3]); c += b;
  b=(b-a) ^ rol(a,mix64zconst[4]); a += c;
  c=(c-b) ^ rol(b,mix64zconst[5]); b += a;
    }
    static inline void final(c64& a, c64& b, c64& c)
    {
  c=(c^b) - rol(b,mix64zconst[6]);
  a=(a^c) - rol(c,mix64zconst[7]);
  b=(b^a) - rol(a,mix64zconst[8]);
  c=(c^b) - rol(b,mix64zconst[9]);/*
  a=(a^c) - rol(c,mix64zconst[10]);
  b=(b^a) - rol(a,mix64zconst[11]);
  c=(c^b) - rol(b,mix64zconst[12]);*/
    }

    static RetT test(byte data[96])
    {
        c64 a(UINT64_C(0x9e3779b97f4a7c15 + 96));
        c64 b(a), c(a);
        a += *(c64*)&data[0];
        b += *(c64*)&data[8];
        c += *(c64*)&data[16];
        mix(a,b,c);
        a += *(c64*)&data[24];
        b += *(c64*)&data[32];
        c += *(c64*)&data[40];
        mix(a,b,c);
        a += *(c64*)&data[48];
        b += *(c64*)&data[56];
        c += *(c64*)&data[64];
        mix(a,b,c);
        a += *(c64*)&data[72];
        b += *(c64*)&data[80];
        c += *(c64*)&data[88];
        mix(a,b,c);
        final(a,b,c);
        return c;
    }
};

int mix64_2zconst[2*3+4] = {  0 }; //41, 58, 16, 34, 19,  7,  4, 30, 50, 59 };
static c64_2 rol(c64_2 v, int n)
{
    int nrev = 64-n;
    return _mm_slli_epi64(v, n) | _mm_srli_epi64(v, nrev);
    //return (v << n) | (v >> nrev);
}

struct mixer64_2z
{
    static inline void mix(c64_2& a, c64_2& b, c64_2& c)
    {
  a=(a-c) ^ rol(c,mix64_2zconst[0]); c += b;
  b=(b-a) ^ rol(a,mix64_2zconst[1]); a += c;
  c=(c-b) ^ rol(b,mix64_2zconst[2]); b += a;
  a=(a-c) ^ rol(c,mix64_2zconst[3]); c += b;
  b=(b-a) ^ rol(a,mix64_2zconst[4]); a += c;
  c=(c-b) ^ rol(b,mix64_2zconst[5]); b += a;
    }
    static inline void final(c64_2& a, c64_2& b, c64_2& c)
    {
  c=(c^b) - rol(b,mix64_2zconst[6]);
  a=(a^c) - rol(c,mix64_2zconst[7]);
  b=(b^a) - rol(a,mix64_2zconst[8]);
  c=(c^b) - rol(b,mix64_2zconst[9]);/*
  a=(a^c) - rol(c,mix64_2zconst[10]);
  b=(b^a) - rol(a,mix64_2zconst[11]);
  c=(c^b) - rol(b,mix64_2zconst[12]);*/
    }

    static RetT test(byte data[96])
    {
        static const c64 aparts[2] = {UINT64_C(0x9e3779b97f4a7c15 + 96),
                                      UINT64_C(0xf39cc0605cedc834 + 96)};
        c64_2 a(*(c64_2*)aparts);
        c64_2 b(a), c(a);
        a += *(c64_2*)&data[ 0];
        b += *(c64_2*)&data[16];
        c += *(c64_2*)&data[32];
        mix(a,b,c);
        a += *(c64_2*)&data[48];
        b += *(c64_2*)&data[64];
        c += *(c64_2*)&data[80];
        mix(a,b,c);
        final(a,b,c);
        return ((RetT*)&c)[0] ^ ((RetT*)&c)[2];
    }
};
#undef d

int mix64const[4*3] = {26, 38,  8, 12, 31, 19, 30, 15, 13, 10, 24, 25};

struct mixer64
{
    static void mix(c64& a, c64& b, c64& c)
    {
  a=(a-b-c) ^ sr(c,mix64const[0]);
  b=(b-c-a) ^ sl(a,mix64const[1]);
  c=(c-a-b) ^ sr(b,mix64const[2]);

  a=(a-b-c) ^ sr(c,mix64const[3]);
  b=(b-c-a) ^ sl(a,mix64const[4]);
  c=(c-a-b) ^ sr(b,mix64const[5]);

  a=(a-b-c) ^ sr(c,mix64const[6]);
  b=(b-c-a) ^ sl(a,mix64const[7]);
  c=(c-a-b) ^ sr(b,mix64const[8]);

  a=(a-b-c) ^ sr(c,mix64const[9]);
  b=(b-c-a) ^ sl(a,mix64const[10]);
  c=(c-a-b) ^ sr(b,mix64const[11]);
    }

    static RetT test(byte data[96])
    {
        c64 a(UINT64_C(0x9e3779b97f4a7c15));
        c64 b(a), c(0);
        a += *(c64*)&data[0];
        b += *(c64*)&data[8];
        c += *(c64*)&data[16];
        mix(a,b,c);
        a += *(c64*)&data[24];
        b += *(c64*)&data[32];
        c += *(c64*)&data[40];
        mix(a,b,c);
        a += *(c64*)&data[48];
        b += *(c64*)&data[56];
        c += *(c64*)&data[64];
        mix(a,b,c);
        a += *(c64*)&data[72];
        b += *(c64*)&data[80];
        c += *(c64*)&data[88];
        mix(a,b,c);
        return c;
    }
};

int mix128zconst[2*3+4] = {  79,124, 60, 74,115,101, 60, 20, 91,106};

struct mixer128z
{
    static inline void mix(c128& a, c128& b, c128& c)
    {
  a=(a-c) ^ rol(c,mix128zconst[0]); c += b;
  b=(b-a) ^ rol(a,mix128zconst[1]); a += c;
  c=(c-b) ^ rol(b,mix128zconst[2]); b += a;
  a=(a-c) ^ rol(c,mix128zconst[3]); c += b;
  b=(b-a) ^ rol(a,mix128zconst[4]); a += c;
  c=(c-b) ^ rol(b,mix128zconst[5]); b += a;
    }
    static inline void final(c128& a, c128& b, c128& c)
    {
  c=(c^b) - rol(b,mix128zconst[6]);
  a=(a^c) - rol(c,mix128zconst[7]);
  b=(b^a) - rol(a,mix128zconst[8]);
  c=(c^b) - rol(b,mix128zconst[9]);/*
  a=(a^c) - rol(c,mix128zconst[10]);
  b=(b^a) - rol(a,mix128zconst[11]);
  c=(c^b) - rol(b,mix128zconst[12]);*/
    }

    static RetT test(byte data[96])
    {
        c64 aparts[2] = {UINT64_C(0xf39cc0605cedc834), UINT64_C(0x9e3779b97f4a7c15)};
        c128 a(*(c128*)aparts);
        a += 96;
        c128 b(a), c(a);
        a += *(c128*)&data[0];
        b += *(c128*)&data[16];
        c += *(c128*)&data[32];
        mix(a,b,c);
        a += *(c128*)&data[48];
        b += *(c128*)&data[64];
        c += *(c128*)&data[80];
        mix(a,b,c);
        final(a,b,c);
        return c;
    }
};

int mix128const[5*3] = {56, 77, 60, 75, 30, 33, 38, 58, 71, 88, 65, 23, 15,101, 71};

struct mixer128
{
    static void mix(c128& a, c128& b, c128& c)
    {
  a=(a-b-c) ^ sr(c,mix128const[0]);
  b=(b-c-a) ^ sl(a,mix128const[1]);
  c=(c-a-b) ^ sr(b,mix128const[2]);

  a=(a-b-c) ^ sr(c,mix128const[3]);
  b=(b-c-a) ^ sl(a,mix128const[4]);
  c=(c-a-b) ^ sr(b,mix128const[5]);

  a=(a-b-c) ^ sr(c,mix128const[6]);
  b=(b-c-a) ^ sl(a,mix128const[7]);
  c=(c-a-b) ^ sr(b,mix128const[8]);

  a=(a-b-c) ^ sr(c,mix128const[9]);
  b=(b-c-a) ^ sl(a,mix128const[10]);
  c=(c-a-b) ^ sr(b,mix128const[11]);

  a=(a-b-c) ^ sr(c,mix128const[12]);
  b=(b-c-a) ^ sl(a,mix128const[13]);
  c=(c-a-b) ^ sr(b,mix128const[14]);
    }

    static RetT test(byte data[96])
    {
        c64 aparts[2] = {UINT64_C(0xf39cc0605cedc834), UINT64_C(0x9e3779b97f4a7c15)};
        c128 a(*(c128*)aparts);
        c128 b(a), c(0);
        a += *(c128*)&data[0];
        b += *(c128*)&data[16];
        c += *(c128*)&data[32];
        mix(a,b,c);
        a += *(c128*)&data[48];
        b += *(c128*)&data[64];
        c += *(c128*)&data[80];
        mix(a,b,c);
        return c;
    }
};


randctx mutctx;

template<typename Mixer>
long double Evaluate(const char* name, bool echo=true)
{
    randctx rctx;
    randinit(&rctx, 0);

    unsigned min_differ=32, max_differ=0;
    unsigned long long total_differ=0;

    int changes[96*8][32] = { { 0 } };

    enum { n_do_tests = 1000 };

    for(unsigned n_tests=0; n_tests<n_do_tests; ++n_tests)
    {
        byte data[96];
        for(int a=0; a<96; a+=4)
            *(c32*)&data[a] = rand(&rctx);

        RetT test1 = Mixer::test(data);

        for(unsigned b = 0; b < 96*8; ++b)
        {
            unsigned char dbup = data[b/8];
            data[b/8] ^= 1 << (b % 8); // mutate the data
            RetT test2 = Mixer::test(data);
            data[b/8] = dbup;          // undo the mutation

            // Count the differing bits in test1 and test2
            unsigned n_differ = 0;
            for(unsigned long bit=1, c=0; c<32; ++c, bit*=2)
                if((test1 & bit) ^ (test2 & bit))
                {
                    n_differ += 1;
                    changes[b][c] += 1;
                }

            total_differ += n_differ;
            if(n_differ < min_differ) min_differ = n_differ;
            if(n_differ > max_differ) max_differ = n_differ;
        }
    }

    long double avg_differ = total_differ/(long double)(n_do_tests*96*8);
    /*
    long double score = (avg_differ - 16.0); if(score<0)score=-score;
    score += (max_differ-min_differ)*(long double)1e-6;
    */

    long score_l = 0, changes_min = 96*8*32*n_do_tests, changes_max = 0;
    for(int b=0; b<96*8; ++b)
        for(int n=0; n<32; ++n)
        {
            int c = changes[b][n] - n_do_tests/2; if(c < 0) c = -c;
            score_l += c;
            if(changes[b][n] < changes_min) changes_min = changes[b][n];
            if(changes[b][n] > changes_max) changes_max = changes[b][n];
        }
    long double score = score_l / (long double)(96*8*32*n_do_tests);

    if(echo)
    {
        printf("%s: Differ: %u..%u, avg %.6Lf, score %.10Lf (chg %.0f..%.0f)\n",
            name, min_differ,max_differ, avg_differ, score,
            changes_min*100.0/n_do_tests,
            changes_max*100.0/n_do_tests);
        for(int b=0; b<96*8; ++b)
        {
            printf("Per bit[%3d]:", b);
            for(int n=0; n<32; ++n)
                printf("%3.0f", changes[b][n]*100.0/(n_do_tests));
            printf("\n");
        }
        fflush(stdout);
    }

    return score;
}

template<int nconsts, int nbits>
struct testset
{
    int values[nbits];
    long double score;

    void mutate(int amount)
    {
        while(amount > 0)
        {
            int& v = values[rand(&mutctx) % (nbits)];
            int r = rand(&mutctx);
            int p = 1+(r&7);
            amount -= p;
            if(r&256) p=-p;
            v += p;
            if(v >= nbits) v = -nbits + 1 + (v-nbits);
            else if(v <= -nbits) v = nbits - 1 - (-nbits - v);

            v &= nbits-1;
        }
    }

    bool operator< (const testset& b) const
    {
        return score < b.score;
    }
};


template<typename Mixer, int nconsts, int nbits>
void Improve(int consttable[nconsts])
{
    long double bestscore = 100;
  {
    char Buf[512]="";

    for(int a=0; a<nconsts; ++a)
    {
        char Buf2[64];
        sprintf(Buf2, ",%3d", consttable[a]);
        strcat(Buf, Buf2);
    }
    strcat(Buf, "[REI]");
    bestscore = Evaluate<Mixer>(Buf);
  }

    std::vector<testset<nconsts,nbits> > testsets;
    std::vector<testset<nconsts,nbits> > models;
    testset<nconsts,nbits> best;
    memcpy(best.values, consttable, sizeof(int)*nconsts);

    for(;;)
    {
        // Always append back a completely randomized one
        testset<nconsts,nbits> m;
        for(int a=0; a<nconsts; ++a)
            m.values[a] = rand(&mutctx) % nbits;
        testsets.push_back(m);

        for(size_t a=0; a<testsets.size(); ++a)
        {
            memcpy(consttable, testsets[a].values, sizeof(int)*nconsts);

            char Buf[64];
            sprintf(Buf, "cand %lu", a);

            testsets[a].score = Evaluate<Mixer> (Buf, false);
        }

        std::sort(testsets.begin(), testsets.end());

        memcpy(consttable, testsets[0].values, sizeof(int)*nconsts);

        char Buf[512]="";
        for(int a=0; a<nconsts; ++a)
        {
            char Buf2[64];
            sprintf(Buf2, ",%3d", consttable[a]);
            strcat(Buf, Buf2);
        }
        bool badluck = false;
        if(testsets[0].score < bestscore)
        {
            bestscore = testsets[0].score;
            strcat(Buf, "[REC]");
            best = testsets[0];
        }
        else
        {
            strcat(Buf, "     ");
            badluck = true;
        }

        Evaluate<Mixer> (Buf);

        models = testsets;
        testsets.clear();
        if(badluck)
            testsets.push_back(best);

        while(testsets.size() < 90)
        {
            int modelno = rand(&mutctx) % models.size();

            testset<nconsts,nbits> m = models[modelno];

            m.mutate(1+modelno*(nbits/7));

            testsets.push_back(m);
        }
    }
}

int main()
{
    static byte tdata[96];
    mutctx.randa = mixer64z::test(tdata);

    randinit(&mutctx, 0);

    //Improve<mixer128, 5*3, 128> (mix128const);
    //Improve<mixer64, 4*3, 64> (mix64const);
    //Improve<mixer64, 3*3, 64> (mix64const); XXX
    //Improve<mixer32, 3*3, 32> (mix32const);

    Improve<mixer32z, 2*3+4, 32> (mix32zconst);
    //Improve<mixer64z, 2*3+4, 64> (mix64zconst);

    //Improve<mixer64_2z, 2*3+4, 64> (mix64_2zconst);

    //Improve<mixer128z, 2*3+4, 128> (mix128zconst);
}

