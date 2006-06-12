VERSION=1.2.0

include Makefile.sets

#OPTIM += -O0 -fno-inline -g
# -fmudflap
#LDOPTS += -fmudflap -lmudflap

OPTIM += -O3 -ftree-vectorize 

#OPTIM += -O1 -pg -g
#LDOPTS += -pg


OBJS_LZMA += lzma/C/7zip/Compress/LZMA/LZMAEncoder.o
OBJS_LZMA += lzma/C/7zip/Compress/LZ/LZInWindow.o
OBJS_LZMA += lzma/C/7zip/Compress/RangeCoder/RangeCoderBit.o
OBJS_LZMA += lzma/C/7zip/Common/StreamUtils.o
OBJS_LZMA += lzma/C/7zip/Common/OutBuffer.o
OBJS_LZMA += lzma/C/Common/Alloc.o
OBJS_LZMA += lzma/C/Common/CRC.o
OBJS_LZMA += lzma.o

OBJS_MK += $(OBJS_LZMA)
OBJS_MK += mkcromfs.o crc32.o fblock.o memmem.o util.o

OBJS_UN += unmkcromfs.o ../cromfs.o util.o

OBJS_CV += $(OBJS_LZMA)
OBJS_CV += cvcromfs.o util.o

all: mkcromfs unmkcromfs cvcromfs

unmkcromfs: $(OBJS_UN)
	$(CXX) $(CXXFLAGS) $(LDOPTS) -o $@ $^ $(LDLIBS)

mkcromfs: $(OBJS_MK)
	$(CXX) $(CXXFLAGS) $(LDOPTS) -o $@ $^ $(LDLIBS)

cvcromfs: $(OBJS_CV)
	$(CXX) $(CXXFLAGS) $(LDOPTS) -o $@ $^ $(LDLIBS)

mkcromfs.o: fblock.hh lzma.hh datasource.hh crc32.h util.hh mkcromfs_sets.hh boyermoore.hh
fblock.o: fblock.hh boyermoore.hh mkcromfs_sets.hh
unmkcromfs.o: ../cromfs.hh util.hh
cvmkcromfs.o: ../cromfs-defs.hh lzma.hh util.hh

include depfun.mak

clean:
	rm -f $(OBJS_MK) $(OBJS_UN)
	rm -f mkcromfs unmkcromfs
