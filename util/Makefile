VERSION=1.4.1

include Makefile.sets

#OPTIM += -O0 -fno-inline -g
# -fmudflap
#LDOPTS += -fmudflap -lmudflap

OPTIM += -O3
#OPTIM += -O1 -g
# -ftree-vectorize 

#OPTIM += -O1 -pg -g
#LDOPTS += -pg

CPPFLAGS += -I../lib

# multithread versions of lzma -- Windows only
#CPPFLAGS += -DCOMPRESS_MF_MT
#CPPFLAGS += -DCOMPRESS_MT

#OBJS_LZMA += ../lib/lzma/CPP/7zip/Compress/LZMA_Alone/LzmaAlone.o
OBJS_LZMA += ../lib/lzma/CPP/7zip/Compress/LZMA/LZMAEncoder.o
OBJS_LZMA += ../lib/lzma/CPP/7zip/Compress/RangeCoder/RangeCoderBit.o
OBJS_LZMA += ../lib/lzma/CPP/7zip/Common/StreamUtils.o
OBJS_LZMA += ../lib/lzma/CPP/7zip/Common/OutBuffer.o
OBJS_LZMA += ../lib/lzma/C/Compress/Lz/MatchFinder.o
OBJS_LZMA += ../lib/lzma/C/Alloc.o
OBJS_LZMA += ../lib/lzma/CPP/Common/CRC.o
OBJS_LZMA += ../lib/lzma/C/7zCrc.o
OBJS_LZMA += ../lib/lzma.o

OBJS_MK += $(OBJS_LZMA)
OBJS_MK += mkcromfs.o fblock.o \
	   ../lib/crc32.o ../lib/memmem.o ../lib/util.o \
	   ../lib/fnmatch.o ../lib/assert++.o ../lib/append.o \
	   ../lib/bwt.o ../lib/mtf.o

OBJS_UN += unmkcromfs.o ../cromfs.o \
	   ../lib/util.o ../lib/fnmatch.o \
	   ../lib/bwt.o ../lib/mtf.o

OBJS_CV += $(OBJS_LZMA)
OBJS_CV += cvcromfs.o ../lib/util.o

all: mkcromfs unmkcromfs cvcromfs

unmkcromfs: $(OBJS_UN)
	$(CXX) $(CXXFLAGS) $(LDOPTS) -o $@ $^ $(LDLIBS)

mkcromfs: $(OBJS_MK)
	$(CXX) $(CXXFLAGS) $(LDOPTS) -o $@ $^ $(LDLIBS)

cvcromfs: $(OBJS_CV)
	$(CXX) $(CXXFLAGS) $(LDOPTS) -o $@ $^ $(LDLIBS)

lzmatest: lzmatest.o $(OBJS_LZMA)
	$(CXX) $(CXXFLAGS) $(LDOPTS) -o $@ $^ $(LDLIBS)

bwt_test: bwt_test.o ../lib/bwt.o
	$(CXX) $(CXXFLAGS) $(LDOPTS) -o $@ $^ $(LDLIBS)
wfc_test: wfc_test.o ../lib/wfc.o
	$(CXX) $(CXXFLAGS) $(LDOPTS) -o $@ $^ $(LDLIBS)
st4_test: st4_test.o ../lib/st4.o
	$(CXX) $(CXXFLAGS) $(LDOPTS) -o $@ $^ $(LDLIBS)

include depfun.mak

clean:
	rm -f $(OBJS_MK) $(OBJS_UN)
	rm -f mkcromfs unmkcromfs
