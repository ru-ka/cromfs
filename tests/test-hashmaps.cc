#include "../lib/cromfs-hashmap_lzo.cc"
#include "../lib/cromfs-hashmap_googlesparse.cc"
#include "../lib/cromfs-hashmap_lzo_sparse.cc"
#include "../lib/cromfs-hashmap_sparsefile.cc"
#include "../lib/newhash.h"

typedef uint_least32_t hashtype;
typedef int            valuetype;

const char* GetTempDir()
{
    const char* t;
    t = std::getenv("TEMP"); if(t) return t;
    t = std::getenv("TMP"); if(t) return t;
    return "/tmp";
}

valuetype Permutate(hashtype t)
{
    return newhash_calc((const unsigned char*)&t, sizeof(t));
}

int key_errors = 0, value_errors = 0, hit_errors = 0;

template<typename HashmapT>
void RunTests(const std::string& TestName)
{
    HashmapT map;

    printf("Testing %s...\n", TestName.c_str());

    std::map<hashtype, valuetype> ref;
    printf("- Populating the hashmap...\n");
    for(size_t initcounter=0; initcounter<250000; ++initcounter)
    {
        hashtype  h = Permutate(initcounter);
        valuetype v = initcounter*7;
        map.set(h, v);
        ref[h] = v;

        if(initcounter%7==0 || initcounter%23==0)
        {
            h = Permutate(initcounter-4);
            map.unset(h);
            ref.erase(h);
        }
    }

    printf("- Checking the hashmap...\n");
    int n_key_error = 0, n_value_error = 0;
    for(std::map<hashtype, valuetype>::const_iterator
        i = ref.begin();
        i != ref.end();
        ++i)
    {
        if(!map.has(i->first))
        {
            if(i->second == 0)
            {
                printf("- Key %08X error. However, the expected value is %d, so this error is forgiven in order to not disavantage CacheFile.\n",
                    i->first, i->second);
            }
            else
            {
                if(n_key_error > 0) printf("\33[1A- %d * ", n_key_error+1);
                n_key_error += 1;
                printf("- Key %08X error\n", i->first);
                ++key_errors;
            }
        }
        else
        {
            valuetype tmp;
            map.extract(i->first, tmp);
            if(tmp != i->second)
            {
                if(n_value_error > 0) printf("\33[1A- %d * ", n_value_error+1);
                n_value_error += 1;
                printf("- Data error, expect %d get %d for key %08X\n", i->second, tmp, i->first);
                ++value_errors;
            }
        }
    }

    printf("- Checking for false positives...\n");
    for(size_t testcounter=0; testcounter<250000; ++testcounter)
    {
        hashtype h = Permutate(testcounter*117);

        const char* shouldbe = ref.find(h) != ref.end() ? "found" : "not found";
        const char*      got = map.has(h)               ? "found" : "not found";

        if(*shouldbe != *got)
        {
            if(*shouldbe == 'f' && ref[h] == 0) continue; // forgive

            printf("- Find error for %08X - should be %s, was %s", h, shouldbe, got);
            if(*got == 'f')
            {
                valuetype v;
                map.extract(h, v);
                printf(" - value gotten is %d\n", v);
            }
            else
                printf("\n");
            ++hit_errors;
        }
    }
}


int main()
{
    RunTests<CompressedHashLayer<hashtype,valuetype> >        ("lzo");
    RunTests<CompressedHashLayer_Sparse<hashtype,valuetype> > ("lzo_sparse");
    RunTests<CacheFile<hashtype,valuetype> >                  ("sparsefile");
    RunTests<GoogleSparseMap<hashtype,valuetype> >            ("googlesparse");

    if(key_errors || value_errors || hit_errors)
        printf("Errors found, hash test NOT OK\n");
    else
        printf("All hash tests OK\n");
}
