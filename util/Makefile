VERSION=1.4.0

include Makefile.sets

#OPTIM += -O0 -fno-inline -g
# -fmudflap
#LDOPTS += -fmudflap -lmudflap

OPTIM += -O3
#OPTIM += -O1 -g
# -ftree-vectorize 

#OPTIM += -O1 -pg -g
#LDOPTS += -pg

# multithread versions of lzma -- Windows only
#CPPFLAGS += -DCOMPRESS_MF_MT
#CPPFLAGS += -DCOMPRESS_MT

#OBJS_LZMA += lzma/CPP/7zip/Compress/LZMA_Alone/LzmaAlone.o
OBJS_LZMA += lzma/CPP/7zip/Compress/LZMA/LZMAEncoder.o
OBJS_LZMA += lzma/CPP/7zip/Compress/RangeCoder/RangeCoderBit.o
OBJS_LZMA += lzma/CPP/7zip/Common/StreamUtils.o
OBJS_LZMA += lzma/CPP/7zip/Common/OutBuffer.o
OBJS_LZMA += lzma/C/Compress/Lz/MatchFinder.o
OBJS_LZMA += lzma/C/Alloc.o
OBJS_LZMA += lzma/CPP/Common/CRC.o
OBJS_LZMA += lzma/C/7zCrc.o
OBJS_LZMA += lzma.o

OBJS_MK += $(OBJS_LZMA)
OBJS_MK += mkcromfs.o crc32.o fblock.o memmem.o util.o fnmatch.o assert++.o append.o

OBJS_UN += unmkcromfs.o ../cromfs.o util.o fnmatch.o

OBJS_CV += $(OBJS_LZMA)
OBJS_CV += cvcromfs.o util.o

all: mkcromfs unmkcromfs cvcromfs

unmkcromfs: $(OBJS_UN)
	$(CXX) $(CXXFLAGS) $(LDOPTS) -o $@ $^ $(LDLIBS)

mkcromfs: $(OBJS_MK)
	$(CXX) $(CXXFLAGS) $(LDOPTS) -o $@ $^ $(LDLIBS)

cvcromfs: $(OBJS_CV)
	$(CXX) $(CXXFLAGS) $(LDOPTS) -o $@ $^ $(LDLIBS)

mkcromfs.o: fblock.hh lzma.hh datasource.hh crc32.h util.hh mkcromfs_sets.hh \
	boyermoore.hh append.hh
fblock.o: fblock.hh boyermoore.hh mkcromfs_sets.hh assert++.hh autoptr append.hh
unmkcromfs.o: ../cromfs.hh util.hh
cvmkcromfs.o: ../cromfs-defs.hh lzma.hh util.hh
lzma.o: ../LzmaDecode.h ../LzmaDecode.c ../LzmaTypes.h


lzmatest: lzmatest.o $(OBJS_LZMA)
	$(CXX) $(CXXFLAGS) $(LDOPTS) -o $@ $^ $(LDLIBS)

include depfun.mak

clean:
	rm -f $(OBJS_MK) $(OBJS_UN)
	rm -f mkcromfs unmkcromfs
