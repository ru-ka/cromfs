VERSION=1.1.2.1

include Makefile.sets

#CXXFLAGS += -O1 -fno-inline -g
CXXFLAGS += -O3

OBJS += lzma/C/7zip/Compress/LZMA/LZMAEncoder.o
OBJS += lzma/C/7zip/Compress/LZ/LZInWindow.o
OBJS += lzma/C/7zip/Compress/RangeCoder/RangeCoderBit.o
OBJS += lzma/C/7zip/Common/StreamUtils.o
OBJS += lzma/C/7zip/Common/OutBuffer.o
OBJS += lzma/C/Common/Alloc.o
OBJS += lzma/C/Common/CRC.o

OBJS += lzma.o main.o crc32.o

all: mkcromfs

mkcromfs: $(OBJS)
	$(CXX) $(CXXFLAGS) -o $@ $(OBJS) $(LDLIBS)


include depfun.mak

clean:
	rm -f $(OBJS)
