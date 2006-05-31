VERSION=1.1.4.3

include Makefile.sets

#OPTIM += -O0 -fno-inline -g -fmudflap
OPTIM += -O3 -ftree-vectorize 

#LDOPTS += -fmudflap -lmudflap

OBJS += lzma/C/7zip/Compress/LZMA/LZMAEncoder.o
OBJS += lzma/C/7zip/Compress/LZ/LZInWindow.o
OBJS += lzma/C/7zip/Compress/RangeCoder/RangeCoderBit.o
OBJS += lzma/C/7zip/Common/StreamUtils.o
OBJS += lzma/C/7zip/Common/OutBuffer.o
OBJS += lzma/C/Common/Alloc.o
OBJS += lzma/C/Common/CRC.o

OBJS += lzma.o main.o crc32.o fblock.o memmem.o

all: mkcromfs

mkcromfs: $(OBJS)
	$(CXX) $(CXXFLAGS) $(LDOPTS) -o $@ $(OBJS) $(LDLIBS)

main.o: fblock.hh memmem.h
fblock.o: fblock.hh memmem.h

include depfun.mak

clean:
	rm -f $(OBJS)
