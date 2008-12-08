VERSION=1.5.5

include Makefile.sets

#OPTIM += -O0 -fno-inline -g
# -fmudflap
#LDOPTS += -fmudflap -lmudflap

#OPTIM += -O3
OPTIM += -O1 -g -fno-inline
#OPTIM += -O3 -g -fno-inline -finline-level=0 -debug extended
#OPTIM += -O0 -g -fno-inline
# -ftree-vectorize 

#OPTIM += -O1 -pg -g
#LDOPTS += -pg

CPPFLAGS += -I../lib -I..

# multithread versions of lzma -- Windows only
#CPPFLAGS += -DCOMPRESS_MF_MT
#CPPFLAGS += -DCOMPRESS_MT

OBJS_LZMADEC += ../lib/lzma/C/LzmaDec.o
OBJS_LZMA += $(OBJS_LZMADEC)
OBJS_LZMA += ../lib/lzma/C/LzmaEnc.o
OBJS_LZMA += ../lib/lzma/C/LzFind.o
OBJS_LZMA += ../lib/lzma.o


OBJS_MK += $(OBJS_LZMA)
OBJS_MK += mkcromfs.o \
	   ../lib/fadvise.o \
	   ../lib/newhash.o ../lib/util.o \
	   ../lib/fnmatch.o ../lib/assert++.o ../lib/append.o \
	   ../lib/sparsewrite.o \
	   ../lib/longfilewrite.o \
	   ../lib/cromfs-inodefun.o \
	   ../lib/cromfs-directoryfun.o \
	   ../lib/cromfs-blockindex.o \
	   ../lib/cromfs-fblockfun.o \
	   ../lib/cromfs-blockifier.o \
	   ../lib/boyermoore.o

OBJS_UN += unmkcromfs.o ../cromfs.o \
	   ../lib/fadvise.o \
	   ../lib/util.o ../lib/fnmatch.o \
	   ../lib/sparsewrite.o \
	   ../lib/longfilewrite.o ../lib/cromfs-inodefun.o \
	   $(OBJS_LZMADEC)

OBJS_CV += $(OBJS_LZMA)
OBJS_CV += cvcromfs.o ../lib/util.o ../lib/sparsewrite.o \
	   ../lib/fadvise.o \
	   ../lib/longfilewrite.o

all: mkcromfs unmkcromfs cvcromfs

unmkcromfs: $(OBJS_UN)
	$(CXX) $(CXXFLAGS) $(LDOPTS) -o $@ $^ $(LDFLAGS) $(LDLIBS)

mkcromfs: $(OBJS_MK)
	$(CXX) $(CXXFLAGS) $(LDOPTS) -o $@ $^ $(LDFLAGS) $(LDLIBS)

cvcromfs: $(OBJS_CV)
	$(CXX) $(CXXFLAGS) $(LDOPTS) -o $@ $^ $(LDFLAGS) $(LDLIBS)

lzmatest: lzmatest.o $(OBJS_LZMA)
	$(CXX) $(CXXFLAGS) $(LDOPTS) -o $@ $^ $(LDFLAGS) $(LDLIBS)

.libdepend: ../lib/.depend
	perl -pe 's@([-+a-zA-Z0-9._/]+)@../lib/$$1@g' < "$<" > "$@"
../lib/.depend:
	make -C ../lib depend
include depfun.mak
include .libdepend

clean:
	rm -f $(OBJS_MK) $(OBJS_UN) $(OBJS_CV)
	rm -f mkcromfs unmkcromfs
