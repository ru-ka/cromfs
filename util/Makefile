VERSION=1.0.5

include Makefile.sets

CXXFLAGS += -O3 -fno-inline

OBJS += lzma/C/7zip/Compress/LZMA/LZMAEncoder.o
OBJS += lzma/C/7zip/Compress/LZ/LZInWindow.o
OBJS += lzma/C/7zip/Compress/RangeCoder/RangeCoderBit.o
OBJS += lzma/C/7zip/Common/StreamUtils.o
OBJS += lzma/C/7zip/Common/OutBuffer.o
OBJS += lzma/C/Common/Alloc.o
OBJS += lzma/C/Common/CRC.o

OBJS += lzma.o main.o

LDLIBS += -lssl
	
all: mkcromfs

mkcromfs: $(OBJS)
	$(CXX) $(CXXFLAGS) -o $@ $(OBJS) $(LDLIBS)


include depfun.mak

clean:
	rm -f $(OBJS)
