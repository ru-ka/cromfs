#define _LARGEFILE64_SOURCE
#define __STDC_CONSTANT_MACROS

#include "lzma.hh"
#include "cromfs-defs.hh"
#include "longfileread.hh"
#include "longfilewrite.hh"
#include "util.hh"

#include <getopt.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <errno.h>
#include <cstdlib>
#include <vector>

#include <cstdio>

int LZMA_HeavyCompress = 0;
double RootDirInflateFactor = 1;
double InotabInflateFactor  = 1;
double BlktabInflateFactor  = 1;

struct BlockToucher
{
    virtual bool NeedsData() const { return false; }
    virtual void Got(std::vector<unsigned char>& ) { }

    virtual ~BlockToucher() {}
};
struct InodeToucher: public BlockToucher
{
    struct Oper { uint_fast64_t offs; unsigned width; uint_fast64_t value; };
    std::map<uint_fast64_t, Oper> Read, Write;

    virtual bool NeedsData() const { return !Read.empty() || !Write.empty(); }

    void SetRead(uint_fast64_t offs, unsigned w)
        { Oper o; o.offs=offs; o.width=w; o.value=0; Read[offs]=o; }
    void SetWrite(uint_fast64_t offs, unsigned w, uint_fast64_t v)
        { Oper o; o.offs=offs; o.width=w; o.value=v; Write[offs]=o; }
    uint_fast64_t GetRead(uint_fast64_t offs)
        { return Read[offs].value; }

    virtual void Got(std::vector<unsigned char>& Buffer)
    {
        for(std::map<uint_fast64_t, Oper>::iterator
            i = Read.begin(); i != Read.end(); ++i)
            switch(i->second.width)
            {
                case 2: i->second.value = R16(&Buffer[i->first]); break;
                case 4: i->second.value = R32(&Buffer[i->first]); break;
                case 8: i->second.value = R64(&Buffer[i->first]); break;
            }
        for(std::map<uint_fast64_t, Oper>::const_iterator
            i = Write.begin(); i != Write.end(); ++i)
            switch(i->second.width)
            {
                case 2: W16(&Buffer[i->first], i->second.value); break;
                case 4: W32(&Buffer[i->first], i->second.value); break;
                case 8: W64(&Buffer[i->first], i->second.value); break;
            }
    }
};
struct StorageOptToucher: public BlockToucher
{
    uint_fast32_t old_opts; bool read_old;
    uint_fast32_t new_opts; bool write_new;

    StorageOptToucher():old_opts(),read_old(),new_opts(),write_new() { } // -Weffc++

    virtual bool NeedsData() const { return read_old || write_new; }
    virtual void Got(std::vector<unsigned char>& Buffer)
    {
        if(read_old) old_opts = R32(&Buffer[0]);

        if(old_opts &   CROMFS_OPT_24BIT_BLOCKNUMS)
            new_opts |= CROMFS_OPT_24BIT_BLOCKNUMS;

        if(old_opts &   CROMFS_OPT_16BIT_BLOCKNUMS)
            new_opts |= CROMFS_OPT_16BIT_BLOCKNUMS;

        if(old_opts &   CROMFS_OPT_VARIABLE_BLOCKSIZES)
            new_opts |= CROMFS_OPT_VARIABLE_BLOCKSIZES;

        if(write_new) W32(&Buffer[0], new_opts);
    }
};
struct BlkTabConverter: public BlockToucher
{
    uint_fast32_t bsize,fsize;
    bool HadPacked;
    bool WantPacked;

    BlkTabConverter(): bsize(),fsize(),HadPacked(),WantPacked() { } // -Weffc++

    virtual bool NeedsData() const { return HadPacked != WantPacked; }

    virtual void Got(std::vector<unsigned char>& Buffer)
    {
        if(HadPacked != WantPacked)
        {
            const unsigned OldBlockSize = (HadPacked ? 4 : 8);

            unsigned NumBlocks = Buffer.size() / OldBlockSize;

            std::vector<cromfs_block_internal> blktab( NumBlocks);

            for(unsigned a=0; a<NumBlocks; ++a)
            {
                uint_fast32_t fblocknum = 0;
                uint_fast32_t startoffs = 0;
                if(HadPacked)
                    fblocknum = R32(&Buffer[a*4]) / fsize,
                    startoffs = R32(&Buffer[a*4]) % fsize;
                else
                    fblocknum = R32(&Buffer[a*8+0]),
                    startoffs = R32(&Buffer[a*8+4]);
                blktab[a].define(fblocknum, startoffs/*, bsize,fsize*/);
            }

            const unsigned NewBlockSize = (WantPacked ? 4 : 8);

            Buffer.resize(NumBlocks * NewBlockSize);
            for(unsigned a=0; a<NumBlocks; ++a)
            {
                uint_fast32_t fblocknum = blktab[a].fblocknum;
                uint_fast32_t startoffs = blktab[a].startoffs;
                if(WantPacked)
                    W32(&Buffer[a*4],
                        fblocknum * fsize
                      + startoffs);
                else
                    W32(&Buffer[a*8+0], fblocknum),
                    W32(&Buffer[a*8+4], startoffs);
            }
        }
    }
};


static BlockToucher NotTouching;
static uint_fast64_t ConvertBuffer
    (int infd, int outfd,
     const uint_fast64_t in_offs,
     const uint_fast64_t in_size,
     const uint_fast64_t out_offs,
     bool was_compressed,
     bool want_compressed,
     bool recompress,
     BlockToucher& touch_block = NotTouching)
{
    LongFileRead reader(infd, in_offs, in_size);
    std::vector<unsigned char> Buffer(reader.GetAddr(), reader.GetAddr()+in_size);

    std::printf("read %u, ", (unsigned)in_size);
    std::fflush(stdout);

    if(was_compressed && (!want_compressed || recompress || touch_block.NeedsData()))
    {
        Buffer = LZMADeCompress(Buffer);
        was_compressed = false;
    }

    touch_block.Got(Buffer);

    if(want_compressed && !was_compressed)
    {
        Buffer = DoLZMACompress(LZMA_HeavyCompress, Buffer, "data");
    }

    std::printf("written %u", (unsigned)Buffer.size());
    std::fflush(stdout);

    LongFileWrite writer(outfd,0);
    writer.write(&Buffer[0], Buffer.size(), out_offs);
    return Buffer.size();
}

static bool Convert(const std::string& fsfile, const std::string& outfn,
                    int Ver,
                    bool recompress,
                    bool force,
                    uint_least32_t storage_opts)
{
    int outfd = -1;
    int infd = open(fsfile.c_str(), O_RDONLY | O_LARGEFILE);
    if(infd < 0)
    {
    InError:
        perror(fsfile.c_str());
    ErrorExit:
        if(infd != -1) close(infd);
        if(outfd != -1) close(outfd);
        return false;
    }

    if((storage_opts & CROMFS_OPT_SPARSE_FBLOCKS) && Ver < 3)
    {
        std::printf("Warning: Cannot make a sparse filesystem unless version is 3 or greater\n");
        storage_opts &= ~CROMFS_OPT_SPARSE_FBLOCKS;
    }
    if((storage_opts & CROMFS_OPT_PACKED_BLOCKS) && Ver < 3)
    {
        std::printf("Warning: Cannot make packed blocks unless version is 3 or greater\n");
        storage_opts &= ~CROMFS_OPT_PACKED_BLOCKS;
    }

    std::printf("Reading header...\n");

    cromfs_superblock_internal sblock;
    cromfs_superblock_internal::BufferType Superblock;
    if(pread64(infd, Superblock, sizeof(Superblock), 0) == -1) goto InError;

    sblock.ReadFromBuffer(Superblock);

    bool HadCompression  = false;
    bool WantCompression = Ver >= 2;
    uint_least32_t old_storage_opts = 0;

    int OrigVer, SuperblockSize = 0;
    const uint_fast64_t sig  = sblock.sig;
    switch(sig)
    {
        case CROMFS_SIGNATURE_01:
            if(Ver == 1 && !recompress && !force)
            { SameVer:
                std::printf("%s is already version %d\n", fsfile.c_str(), Ver);
                goto ErrorExit;
            }
            OrigVer    = 1;
            SuperblockSize = 0x38;
            HadCompression = false;
            break;
        case CROMFS_SIGNATURE_02:
            if(Ver == 2 && !recompress && !force) goto SameVer;
            OrigVer    = 2;
            SuperblockSize = 0x38;
            HadCompression = true;
            break;
        case CROMFS_SIGNATURE_03:
            if(Ver == 3 && !recompress && !force) goto SameVer;
            OrigVer    = 3;
            SuperblockSize = 0x38;
            HadCompression = true;
            break;
        default:
        {
            std::fprintf(stderr, "%s has unsupported signature\n", fsfile.c_str());
            goto ErrorExit;
        }
    }

    std::printf("Version %02d detected.\n", OrigVer);

    outfd = open(outfn.c_str(), O_WRONLY | O_LARGEFILE | O_CREAT, 0644);
    if(outfd < 0) { perror(outfn.c_str()); goto ErrorExit; }
    ftruncate64(outfd, 0);

    switch(Ver)
    {
        case 1: sblock.sig = CROMFS_SIGNATURE_01; break;
        case 2: sblock.sig = CROMFS_SIGNATURE_02; break;
        case 3: sblock.sig = CROMFS_SIGNATURE_03; break;
    }

    uint_fast64_t write_offs = sblock.GetSize(
        RootDirInflateFactor > 1.0 ||
        InotabInflateFactor > 1.0 ||
        BlktabInflateFactor > 1.0
    );

    std::printf("Converting the root directory inode...\n- ");
    //std::fprintf(stderr, "root goes at %llX\n", write_offs);
    sblock.rootdir_size =
        ConvertBuffer(infd, outfd,
                     sblock.rootdir_offs,
                     sblock.rootdir_size,
                     write_offs, HadCompression,WantCompression, recompress);
    sblock.rootdir_offs = write_offs;
    sblock.rootdir_room = sblock.rootdir_size * RootDirInflateFactor;
    write_offs += sblock.rootdir_room;

    std::printf("\n");
    std::fflush(stdout);

    std::printf("Converting the inotab inode...\n- ");

    StorageOptToucher ReadWriteInotabAttrs;

    ReadWriteInotabAttrs.old_opts  = 0x000000000;
    ReadWriteInotabAttrs.new_opts  = storage_opts;
    ReadWriteInotabAttrs.read_old  = OrigVer >= 3;
    ReadWriteInotabAttrs.write_new = Ver >= 3;

    //std::fprintf(stderr, "inotab goes at %llX\n", write_offs);
    sblock.inotab_size =
        ConvertBuffer(infd, outfd,
                     sblock.inotab_offs,
                     sblock.inotab_size,
                     write_offs,
                     HadCompression,WantCompression,
                     recompress,
                     ReadWriteInotabAttrs);
    sblock.inotab_offs = write_offs;
    sblock.inotab_room = sblock.inotab_size  * InotabInflateFactor;
    write_offs += sblock.inotab_room;

    old_storage_opts = ReadWriteInotabAttrs.old_opts;
    storage_opts     = ReadWriteInotabAttrs.new_opts;

    if((storage_opts & CROMFS_OPT_24BIT_BLOCKNUMS)
    && Ver < 3)
    {
        std::fprintf(stderr, "Error: CROMFS%02u cannot use 24-bit block numbers\n", Ver);
    }
    if((storage_opts & CROMFS_OPT_16BIT_BLOCKNUMS)
    && Ver < 3)
    {
        std::fprintf(stderr, "Error: CROMFS%02u cannot use 16-bit block numbers\n", Ver);
    }

    std::printf("\n");
    std::fflush(stdout);

    std::printf("Converting the block table...\n- ");
    BlkTabConverter ConvertBlkTab;
    ConvertBlkTab.HadPacked = old_storage_opts & CROMFS_OPT_PACKED_BLOCKS;
    ConvertBlkTab.WantPacked = storage_opts & CROMFS_OPT_PACKED_BLOCKS;
    ConvertBlkTab.bsize = sblock.bsize;
    ConvertBlkTab.fsize = sblock.fsize;

    sblock.blktab_size =
        ConvertBuffer(infd,outfd,
                      sblock.blktab_offs,
                      sblock.blktab_size,
                      write_offs, true,true, recompress,
                      ConvertBlkTab);
    sblock.blktab_offs = write_offs;
    sblock.blktab_room = sblock.blktab_size  * BlktabInflateFactor;
    write_offs += sblock.blktab_room;

    std::printf("\n");
    std::fflush(stdout);

    int_fast64_t size_diff = write_offs - sblock.fblktab_offs;

    std::printf("Converting fblocks... size difference so far: %lld bytes\n",
        (long long)size_diff);

    uint_fast64_t read_begin = sblock.fblktab_offs;
    uint_fast64_t read_offs  = read_begin;
    uint_fast64_t read_end   = lseek64(infd, 0, SEEK_END);

    sblock.fblktab_offs = write_offs;

    for(unsigned fblockno=0;;)
    {
        if(read_offs >= read_end) break;

        unsigned char Buf[17];
        ssize_t r = pread64(infd, Buf, 17, read_offs);
        if(r == 0) break;
        if(r < 0) throw errno;
        if(r < 17) throw EINVAL;

        cromfs_fblock_internal fblock;
        fblock.filepos = read_offs+4;
        fblock.length  = R32(Buf+0);
        //uint_fast64_t orig_size = R64(Buf+9);

        double position = (read_offs-read_begin) * 100.0 / (read_end-read_begin);

        std::printf("\r%75s\rfblock %u... (%s)... %.0f%% done: ",
            "", fblockno++, ReportSize(fblock.length).c_str(),
            position);
        std::fflush(stdout);

        uint_fast64_t new_size =
            ConvertBuffer(infd,outfd,
                          fblock.filepos,
                          fblock.length,
                          write_offs+4,
                          true,true, recompress);

        if(storage_opts & CROMFS_OPT_SPARSE_FBLOCKS)
        {
            if(new_size > sblock.fsize)
            {
                std::printf("\n");
                std::fflush(stdout);
                std::fprintf(stderr,
                    "Error: This filesystem cannot be sparse, because there is a compressed\n"
                    "       fblock that is actually larger than the decompressed one.\n"
                    "       Sorry.\n"
                 );
                close(outfd);
                close(infd);
                return false;
            }
        }

        W32(Buf, new_size);
        pwrite64(outfd, Buf, 4, write_offs);

        if(storage_opts & CROMFS_OPT_SPARSE_FBLOCKS)
            write_offs += 4 + sblock.fsize;
        else
            write_offs += 4 + new_size;

        if(old_storage_opts & CROMFS_OPT_SPARSE_FBLOCKS)
            read_offs += 4 + sblock.fsize;
        else
            read_offs += 4 + (uint_fast64_t)fblock.length;
    }

    std::printf("\nWriting header...\n");

    // Last write the modified header
    sblock.WriteToBuffer(Superblock);
    ( LongFileWrite(outfd, 0, sblock.GetSize(), Superblock) );
    ftruncate64(outfd, write_offs);

    std::printf("done.\n");

    struct stat64 st;
    fstat64(outfd, &st);

    std::printf("Output file size: %lld bytes (actual disk space used: %lld bytes)\n",
        (long long)write_offs,
        (long long)(st.st_blocks * 512));

    close(outfd);
    close(infd);

    return true;
}

int main(int argc, char** argv)
{
    int SetVer = 3;
    std::string outfn;
    std::string fsfile;
    bool recompress = false;
    bool force = false;

    uint_least32_t storage_opts = 0;

    for(;;)
    {
        int option_index = 0;
        static struct option long_options[] =
        {
            {"help",        0, 0,'h'},
            {"version",     0, 0,'V'},
            {"setver",      1, 0,'s'},
            {"output",      1, 0,'o'},
            {"recompress",  0, 0,'r'},
            {"sparse",      1, 0,'S'},
            {"force",       0, 0,'f'},
            {"packedblocks",0, 0,'k'},
            {"verbose",     0, 0,'v'},
            {"lzmafastbytes",           1,0,4001},
            {"lzmabits",                1,0,4002},
            {"bwt",                     0,0,2001},
            {"mtf",                     0,0,2002},
            {0,0,0,0}
        };
        int c = getopt_long(argc, argv, "hVs:o:rS:fk", long_options, &option_index);
        if(c==-1) break;
        switch(c)
        {
            case 'V':
            {
                std::printf("%s\n", VERSION);
                return 0;
            }
            case 'h':
            {
                std::printf(
                    "cvcromfs v"VERSION" - Copyright (C) 1992,2009 Bisqwit (http://iki.fi/bisqwit/)\n"
                    "\n"
                    "Converts cromfs images between different versions.\n"
                    "\n"
                    "Usage: cvcromfs [<options>] <source_image> -o <target_image>\n"
                    " --help, -h          This help\n"
                    " --version, -V       Displays version information\n"
                    " --verbose, -v\n"
                    "     -v makes auto/full LZMA compression a bit more verbose\n"
                    "     -vv makes auto/full LZMA compression a lot more verbose\n"
                    " --setver, -s <ver>  Sets the target image to version <ver>\n"
                    "                     Version can be 01, 02 or 03. (Default: 03)\n"
                    " --output, -o <file> Set target imagefilename (mandatory).\n"
                    " --recompress, -r    Recompress fblocks\n"
                    " --force, -f         Force writing the target image even if\n"
                    "                     the version matches and no -r was given\n"
                    "                     (use when changing flags or when you want\n"
                    "                     to reassure the file's sparseness\n"
#if 0
                    " --sparse, -S <opts> Commaseparated list of items to store sparsely\n"
                    "                         -Sf = fblocks\n"
#endif
                    " --packedblocks, -k\n"
                    "     Tells cvcromfs to store blocks in 4 bytes instead of 8 bytes.\n"
                    "     Do not use this option if your cromfs volume packs more than\n"
                    "     4 gigabytes of unique data. Otherwise, use it to save some space.\n"
                    "     This option is available for filesystem version 03 only.\n"
                    " --lzmafastbytes <value>\n"
                    "     Specifies the number of \"fast bytes\" in LZMA compression\n"
                    "     algorithm. Valid values are 5..273. Default is 273.\n"
                    " --lzmabits <pb>,<lp>,<lc>\n"
                    "     Sets the values for PosStateBits, LiteralPosStateBits\n"
                    "     and LiteralContextBits in LZMA properties.\n"
                    "      pb: Default value 0, allowed values 0..4\n"
                    "      lp: Default value 0, allowed values 0..4\n"
                    "      lc: Default value 1, allowed values 0..8\n"
                    "     Further documentation on these values is available in LZMA SDK.\n"
                    "     See file util/lzma/lzma.txt in the cromfs source distribution.\n"
                    "     Alternatively, you can choose \"--lzmabits full\", which will\n"
                    "     try every possible option. Beware it will consume lots of time.\n"
                    "     \"--lzmabits auto\" is a lighter alternative to \"--lzmabits full\".\n"
                    "\n");
                return 0;
            }
            case 's':
            {
                char* arg = optarg;
                long val = strtol(arg, &arg, 10);
                if(val != 1 && val != 2 && val != 3)
                {
                    std::fprintf(stderr, "cvcromfs: The version may be 1, 2 or 3. You gave %ld%s.\n", val, arg);
                    return -1;
                }
                SetVer = val;
                break;
            }
            case 'v':
            {
                ++LZMA_verbose;
                break;
            }
            case 'o':
            {
                outfn = optarg;
                break;
            }
            case 'r':
            {
                recompress = true;
                break;
            }
            case 'f':
            {
                force = true;
                break;
            }
            case 'S':
            {
                for(char* arg=optarg;;)
                {
                    while(*arg==' ')++arg;
                    if(!*arg) break;
                    char* comma = strchr(arg, ',');
                    if(!comma) comma = strchr(arg, '\0');
                    *comma = '\0';
                    int len = strlen(arg);
                    if(len <= 7 && !strncmp("fblocks",arg,len))
                        storage_opts |= CROMFS_OPT_SPARSE_FBLOCKS;
                    else
                    {
                        std::fprintf(stderr, "cvcromfs: Unknown option to -p (%s). See `cvcromfs --help'\n",
                            arg);
                        return -1;
                    }
                    if(!*comma) break;
                    arg=comma+1;
                }
                break;
            }
            case 'k':
            {
                storage_opts |= CROMFS_OPT_PACKED_BLOCKS;
                break;
            }
            case 2001: // bwt
            case 2002: // mtf
            {
                std::fprintf(stderr, "mkcromfs: The --bwt and --mtf options are no longer supported.\n");
                break;
            }
            case 4001: // lzmafastbytes
            {
                char* arg = optarg;
                long size = strtol(arg, &arg, 10);
                if(size < 5 || size > 273)
                {
                    std::fprintf(stderr, "cvcromfs: The number of \"fast bytes\" for LZMA may be 5..273. You gave %ld%s.\n", size,arg);
                    return -1;
                }
                LZMA_NumFastBytes = size;
                break;
            }
            case 4002: // lzmabits
            {
                unsigned arg_index = 0;
                for(char* arg=optarg;;)
                {
                    if(!arg_index && !strcmp(arg, "auto")) { LZMA_HeavyCompress=1; break; }
                    if(!arg_index && !strcmp(arg, "full")) { LZMA_HeavyCompress=2; break; }
                    LZMA_HeavyCompress=0;
                    while(*arg==' ')++arg;
                    if(!*arg) break;
                    char* comma = strchr(arg, ',');
                    if(!comma) comma = strchr(arg, '\0');
                    bool last_comma = !*comma;
                    *comma = '\0';
                    long value = strtol(arg, &arg, 10);
                    long max=4;
                    switch(arg_index)
                    {
                        case 0: // pb
                            LZMA_PosStateBits = value;
                            //std::fprintf(stderr, "pb=%ld\n", value);
                            break;
                        case 1: // lp
                            LZMA_LiteralPosStateBits = value;
                            //std::fprintf(stderr, "lp=%ld\n", value);
                            break;
                        case 2: // lc
                            LZMA_LiteralContextBits = value;
                            //std::fprintf(stderr, "lc=%ld\n", value);
                            max=8;
                            break;
                    }
                    if(value < 0 || value > max || arg_index > 2)
                    {
                        std::fprintf(stderr, "mkcromfs: Invalid value(s) for --lzmabits. See `mkcromfs --help'\n");
                        return -1;
                    }
                    if(last_comma) break;
                    arg=comma+1;
                    ++arg_index;
                }
                break;
            }
        }
    }

    if(argc < optind+1)
    {
    //ArgError:
        std::fprintf(stderr, "cvcromfs: invalid parameters. See `cvcromfs --help'\n");
        return 1;
    }
    fsfile  = argv[optind++];

    if(outfn.size() == 0)
    {
        std::fprintf(stderr, "cvcromfs: output file is mandatory\n");
        return 1;
    }

    return Convert(fsfile, outfn, SetVer, recompress, force, storage_opts) ? 0 : -1;
}
