#define _LARGEFILE64_SOURCE
#define __STDC_CONSTANT_MACROS

#include "lzma.hh"
#include "../cromfs-defs.hh"
#include "util.hh"

#include <getopt.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <errno.h>

#include <vector>

static uint_fast64_t ConvertBuffer
    (int infd, int outfd,
     uint_fast64_t in_offs,
     uint_fast64_t in_size,
     uint_fast64_t out_offs,
     bool was_compressed,
     bool want_compressed,
     bool recompress)
{
    std::vector<unsigned char> Buffer(in_size);
    pread64(infd, &Buffer[0], Buffer.size(), in_offs);
    
    std::printf("read %u, ", (unsigned)Buffer.size());
    std::fflush(stdout);
    
    if(!was_compressed && want_compressed) Buffer = LZMACompress(Buffer);
    if(was_compressed && !want_compressed) Buffer = LZMADeCompress(Buffer);
    
    if(was_compressed && want_compressed && recompress)
    {
        Buffer = LZMACompress(LZMADeCompress(Buffer));
    }
    
    std::printf("written %u", (unsigned)Buffer.size());
    std::fflush(stdout);
    
    pwrite64(outfd, &Buffer[0], Buffer.size(), out_offs);

    return Buffer.size();
}

static bool Convert(const std::string& fsfile, const std::string& outfn,
                    int Ver, bool recompress)
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
    
    std::printf("Reading header...\n");
    
    unsigned char Header[64]; /* max header size */
    if(pread64(infd, Header, sizeof(Header), 0) == -1) goto InError;
    
    int OrigVer, HeaderSize = 0;
    uint_fast64_t sig  = R64(Header+0x0000);
    switch(sig)
    {
        case CROMFS_SIGNATURE_01:
            if(Ver == 1 && !recompress)
            { SameVer:
                std::printf("%s is already version %d\n", fsfile.c_str(), Ver);
                goto ErrorExit;
            }
            OrigVer    = 1;
            HeaderSize = 0x38;
            break;
        case CROMFS_SIGNATURE_02:
            if(Ver == 2 && !recompress) goto SameVer;
            OrigVer    = 2;
            HeaderSize = 0x38;
            break;
        default:
        {
            fprintf(stderr, "%s has unsupported signature\n", fsfile.c_str());
            goto ErrorExit;
        }
    }
    
    std::printf("Version %02d detected.\n", OrigVer);
    
    outfd = open(outfn.c_str(), O_WRONLY | O_LARGEFILE | O_CREAT, 0644);
    if(outfd < 0) { perror(outfn.c_str()); goto ErrorExit; }

    cromfs_superblock_internal sblock;
    sblock.sig = sig;
    sblock.blktab_offs             = R64(Header+0x0008);
    sblock.fblktab_offs            = R64(Header+0x0010);
    sblock.inotab_offs             = R64(Header+0x0018);
    sblock.rootdir_offs            = R64(Header+0x0020);
    sblock.compressed_block_size   = R32(Header+0x0028); /* aka. FSIZE */
    sblock.uncompressed_block_size = R32(Header+0x002C); /* aka. BSIZE */
    sblock.bytes_of_files          = R64(Header+0x0030);

    switch(Ver)
    {
        case 1: W64(Header+0x0000, CROMFS_SIGNATURE_01); break;
        case 2: W64(Header+0x0000, CROMFS_SIGNATURE_02); break;
    }
    
    uint_fast64_t write_offs = HeaderSize;
    
    std::printf("Converting the root directory inode...\n- ");
    W64(Header+0x0020, write_offs);
    uint_fast64_t rootdir_newsize =
        ConvertBuffer(infd, outfd,
                     sblock.rootdir_offs,
                     sblock.inotab_offs - sblock.rootdir_offs,
                     write_offs, OrigVer==2,Ver==2, recompress);
    write_offs += rootdir_newsize;
    std::printf("\n");
    std::fflush(stdout);
    
    std::printf("Converting the inotab inode...\n- ");
    W64(Header+0x0018, write_offs);
    uint_fast64_t inotab_newsize =
        ConvertBuffer(infd, outfd,
                     sblock.inotab_offs,
                     sblock.blktab_offs - sblock.inotab_offs,
                     write_offs, OrigVer==2,Ver==2, recompress);
    write_offs += inotab_newsize;
    std::printf("\n");
    std::fflush(stdout);
    
    std::printf("Converting the block table...\n- ");
    W64(Header+0x0008, write_offs);
    uint_fast64_t blktab_newsize =
        ConvertBuffer(infd,outfd,
                      sblock.blktab_offs,
                      sblock.fblktab_offs - sblock.blktab_offs,
                      write_offs, true,true, recompress);
    write_offs += blktab_newsize;
    std::printf("\n");
    std::fflush(stdout);
    
    int_fast64_t size_diff = write_offs - sblock.fblktab_offs;

    std::printf("Converting fblocks... size difference so far: %lld bytes\n",
        (long long)size_diff);
    W64(Header+0x0010, write_offs);
    
    unsigned fblockno = 0;
    uint_fast64_t startpos = sblock.fblktab_offs;
    uint_fast64_t curpos = startpos;
    uint_fast64_t endpos = lseek64(infd, 0, SEEK_END);
    
    for(;;)
    {
        if(curpos >= endpos) break;
        
        unsigned char Buf[4];
        ssize_t r = pread64(infd, Buf, 4, curpos);
        if(r == 0) break;
        if(r < 0) throw errno;
        if(r < 4) throw EINVAL;
        
        cromfs_fblock_internal fblock;
        fblock.filepos = curpos+4;
        fblock.length  = R32(Buf);
        
        double position = (curpos-startpos) * 100.0 / (endpos-startpos);
        
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
        W32(Buf, new_size);
        pwrite64(outfd, Buf, 4, write_offs);

        write_offs += new_size+4;
        curpos += 4 + (uint_fast64_t)fblock.length;
    }
    
    std::printf("\nWriting header...\n");
    
    // Last write the modified header
    pwrite64(outfd, Header, HeaderSize, 0);
    ftruncate64(outfd, write_offs);
    
    std::printf("done.\n");
    
    close(outfd);
    close(infd);
    
    return true;
}

int main(int argc, char** argv)
{
    int SetVer = 2;
    std::string outfn;
    std::string fsfile;
    bool recompress = false;
    
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
            {0,0,0,0}
        };
        int c = getopt_long(argc, argv, "hVs:o:r", long_options, &option_index);
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
                    "cvcromfs v"VERSION" - Copyright (C) 1992,2007 Bisqwit (http://iki.fi/bisqwit/)\n"
                    "\n"
                    "Converts cromfs images between different versions.\n"
                    "\n"
                    "Usage: cvmkcromfs [<options>] <source_image> -o <target_image>\n"
                    " --help, -h          This help\n"
                    " --version, -V       Displays version information\n"
                    " --setver, -s <ver>  Sets the target image to version <ver>\n"
                    "                     Version can be 01 or 02.\n"
                    " --output, -o <file> Set target imagefilename (mandatory).\n"
                    " --recompress, -r    Recompress fblocks\n"
                    "\n");
                return 0;
            }
            case 's':
            {
                char* arg = optarg;
                long val = strtol(arg, &arg, 10);
                if(val != 1 && val != 2)
                {
                    std::fprintf(stderr, "mkcromfs: The version may be 1 or 2. You gave %ld%s.\n", val, arg);
                    return -1;
                }
                SetVer = val;
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
        }
    }

    if(argc < optind+1)
    {
    ArgError:
        std::fprintf(stderr, "cvcromfs: invalid parameters. See `cvcromfs --help'\n");
        return 1;
    }
    fsfile  = argv[optind++];
    
    if(outfn.size() == 0)
    {
        std::fprintf(stderr, "cvcromfs: output file is mandatory\n");
        return 1;
    }
    
    return Convert(fsfile, outfn, SetVer, recompress) ? 0 : -1;
}
