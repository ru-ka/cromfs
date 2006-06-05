VERSION=1.1.7

include Makefile.sets

#OPTIM += -O0 -fno-inline -g -fmudflap
#LDOPTS += -fmudflap -lmudflap

OPTIM += -O3 -ftree-vectorize 

#OPTIM += -O1 -pg -g
#LDOPTS += -pg


OBJS_MK += lzma/C/7zip/Compress/LZMA/LZMAEncoder.o
OBJS_MK += lzma/C/7zip/Compress/LZ/LZInWindow.o
OBJS_MK += lzma/C/7zip/Compress/RangeCoder/RangeCoderBit.o
OBJS_MK += lzma/C/7zip/Common/StreamUtils.o
OBJS_MK += lzma/C/7zip/Common/OutBuffer.o
OBJS_MK += lzma/C/Common/Alloc.o
OBJS_MK += lzma/C/Common/CRC.o

OBJS_MK += lzma.o mkcromfs.o crc32.o fblock.o memmem.o util.o

OBJS_UN += unmkcromfs.o ../cromfs.o util.o

all: mkcromfs unmkcromfs

unmkcromfs: $(OBJS_UN)
	$(CXX) $(CXXFLAGS) $(LDOPTS) -o $@ $^ $(LDLIBS)

mkcromfs: $(OBJS_MK)
	$(CXX) $(CXXFLAGS) $(LDOPTS) -o $@ $^ $(LDLIBS)

mkcromfs.o: fblock.hh memmem.h
fblock.o: fblock.hh memmem.h
unmkcromfs.o: ../cromfs.hh

include depfun.mak

clean:
	rm -f $(OBJS_MK) $(OBJS_UN)
	rm -f mkcromfs unmkcromfs
