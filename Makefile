VERSION=1.5.4.2
ARCHNAME=cromfs-$(VERSION)

ARCHDIR=archives/
ARCHFILES=\
	Makefile.sets \
	Makefile.sets.in \
	configure \
	\
	COPYING progdesc.php \
	\
	doc/FORMAT \
	doc/README.html \
	doc/ChangeLog \
	doc/ImplementationGuide.txt \
	doc/WriteAccess.txt \
	doc/BlockIndexing.txt \
	doc/size_demo.txt \
	doc/boxart.png \
	doc/boxart-src.zip \
	cromfs.spec \
	\
	cromfs.cc cromfs.hh cromfs-defs.hh \
	cromfs-write.cc cromfs-write.hh \
	fuse-ops.cc fuse-ops.hh \
	fuse-main.c \
	\
	util/Makefile.sets util/Makefile util/depfun.mak \
	lib/Makefile.sets lib/Makefile lib/depfun.mak \
	\
	util/mkcromfs.cc \
	util/unmkcromfs.cc \
	util/cvcromfs.cc \
	util/mkcromfs_sets.hh \
	\
	lib/cromfs-inodefun.cc lib/cromfs-inodefun.hh \
	lib/cromfs-directoryfun.cc lib/cromfs-directoryfun.hh \
	lib/cromfs-fblockfun.cc lib/cromfs-fblockfun.hh \
	lib/cromfs-blockindex.cc lib/cromfs-blockindex.hh \
	lib/cromfs-blockifier.cc lib/cromfs-blockifier.hh \
	\
	lib/endian.hh \
	lib/hash.hh \
	lib/simd.hh \
	lib/threadfun.hh \
	lib/threadfun_none.hh \
	lib/threadfun_openmp.hh \
	lib/threadfun_pthread.hh \
	lib/threadworkengine.hh lib/threadworkengine.tcc \
	lib/duffsdevice.hh \
	lib/stringsearchutil.hh \
	lib/datasource.hh \
	lib/datareadbuf.hh \
	lib/autoclosefd.hh \
	lib/bucketcontainer.hh \
	lib/datacache.hh \
	lib/mmapping.hh \
	lib/fadvise.cc lib/fadvise.hh \
	lib/lzma.cc lib/lzma.hh \
	lib/util.cc lib/util.hh \
	lib/append.cc lib/append.hh \
	lib/fnmatch.cc lib/fnmatch.hh \
	lib/crc32.h lib/crc32.cc \
	lib/newhash.h lib/newhash.cc \
	lib/assert++.hh lib/assert++.cc \
	lib/sparsewrite.cc lib/sparsewrite.hh \
	lib/longfileread.hh \
	lib/longfilewrite.hh lib/longfilewrite.cc \
	lib/boyermooreneedle.hh \
	lib/boyermoore.hh lib/boyermoore.cc \
	lib/superstringfinder.hh \
	lib/asymmetrictsp.hh \
	lib/autoptr \
	\
	lib/range.hh lib/range.tcc \
	lib/rangeset.hh lib/rangeset.tcc \
	lib/rangemultimap.hh lib/rangemultimap.tcc \
	\
	lib/LzmaDecode.c lib/LzmaDecode.h lib/LzmaTypes.h \
	\
	lib/lzma/ORIGIN \
	lib/lzma/LGPL.txt \
	lib/lzma/lzma.txt \
	\
	lib/lzma/C/7zCrc.c \
	lib/lzma/C/7zCrc.h \
	lib/lzma/C/Alloc.c \
	lib/lzma/C/Alloc.h \
	lib/lzma/C/IStream.h \
	lib/lzma/C/Types.h \
	lib/lzma/CPP/Common/CRC.cpp \
	lib/lzma/CPP/Common/Defs.h \
	lib/lzma/CPP/Common/MyCom.h \
	lib/lzma/CPP/Common/MyException.h \
	lib/lzma/CPP/Common/MyGuidDef.h \
	lib/lzma/CPP/Common/MyInitGuid.h \
	lib/lzma/CPP/Common/MyUnknown.h \
	lib/lzma/CPP/Common/MyWindows.h \
	lib/lzma/CPP/Common/NewHandler.h \
	lib/lzma/CPP/Common/StdAfx.h \
	lib/lzma/CPP/Common/Types.h \
	lib/lzma/C/Compress/Lz/LzHash.h \
	lib/lzma/C/Compress/Lz/MatchFinder.c \
	lib/lzma/C/Compress/Lz/MatchFinder.h \
	lib/lzma/C/Compress/Lzma/LzmaDecode.c \
	lib/lzma/C/Compress/Lzma/LzmaDecode.h \
	lib/lzma/C/Compress/Lzma/LzmaTypes.h \
	lib/lzma/CPP/7zip/ICoder.h \
	lib/lzma/CPP/7zip/IStream.h \
	lib/lzma/CPP/7zip/Common/InBuffer.h \
	lib/lzma/CPP/7zip/Common/OutBuffer.cpp \
	lib/lzma/CPP/7zip/Common/OutBuffer.h \
	lib/lzma/CPP/7zip/Common/StdAfx.h \
	lib/lzma/CPP/7zip/Common/StreamUtils.cpp \
	lib/lzma/CPP/7zip/Common/StreamUtils.h \
	lib/lzma/CPP/7zip/Compress/LZMA/LZMAEncoder.cpp \
	lib/lzma/CPP/7zip/Compress/LZMA/LZMAEncoder.h \
	lib/lzma/CPP/7zip/Compress/LZMA/LZMA.h \
	lib/lzma/CPP/7zip/Compress/LZMA/StdAfx.h \
	lib/lzma/CPP/7zip/Compress/RangeCoder/RangeCoderBit.cpp \
	lib/lzma/CPP/7zip/Compress/RangeCoder/RangeCoderBit.h \
	lib/lzma/CPP/7zip/Compress/RangeCoder/RangeCoderBitTree.h \
	lib/lzma/CPP/7zip/Compress/RangeCoder/RangeCoder.h \
	lib/lzma/CPP/7zip/Compress/RangeCoder/RangeCoderOpt.h \
	lib/lzma/CPP/7zip/Compress/RangeCoder/StdAfx.h \
	\
	tests/run.sh \
	tests/a/fblock.hh tests/a/lzma.hh \
	tests/a/md5-copy.hh tests/a/md5.hh \
	tests/a/util.hh \
	tests/a/fifo tests/a/fifo-copy \
	tests/a/dir2 tests/a/dir2/util.cc \
	tests/a/dir2/util.hh \
	tests/a/sparse.bin \
	tests/test-boyermoore.cc

include Makefile.sets

#CXXFLAGS += -O1 -fno-inline -g
CXXFLAGS += -O3 -fno-rtti

CPPFLAGS += `pkg-config --cflags fuse`
LDLIBS   += `pkg-config --libs fuse`

OBJS=\
	cromfs.o fuse-ops.o fuse-main.o \
	lib/cromfs-inodefun.o \
	lib/fadvise.o lib/util.o

LDLIBS += $(FUSELIBS)

DEPFUN_INSTALL=ignore

PROGS = cromfs-driver cromfs-driver-static util/mkcromfs util/unmkcromfs util/cvcromfs
DOCS  = doc/FORMAT README.html doc/ChangeLog doc/*.txt

all-strip: all FORCE
	- strip cromfs-driver util/mkcromfs util/unmkcromfs util/cvcromfs
	@echo
	@echo Finished compiling. These were created:
	@ls -al $(PROGS)

all: $(PROGS)

cromfs-driver: $(OBJS)
	$(CXX) $(CXXFLAGS) -o $@ $(OBJS) $(LDFLAGS) $(LDLIBS)

cromfs-driver-static: $(OBJS)
	$(CXX) -static $(CXXFLAGS) -o $@ $(OBJS) $(LDFLAGS) $(LDLIBS)
	- strip -R.comment $@
	# Note: It does not matter if upx cannot run.
	- upx --best $@

util/mkcromfs: FORCE
	make -C util mkcromfs

util/unmkcromfs: FORCE
	make -C util unmkcromfs

util/cvcromfs: FORCE
	make -C util cvcromfs

clean:
	rm -rf $(OBJS) $(PROGS) install
	rm -f cromfs-driver-static.??? configure.log
	make -C util clean

install: $(PROGS) FORCE
	- mkdir install install/progs install/docs
	cp -p $(PROGS) install/progs/
	cp -p $(DOCS) install/docs/
	- strip install/progs/*
	@echo "*****************************************"
	@echo "The 'install' directory was prepared. Copy the contents to the locations you see fit."
	@echo "*****************************************"

test: $(PROGS) FORCE
	cd tests && ./run.sh

.libdepend: lib/.depend
	perl -pe 's@([-+a-zA-Z0-9._/]+)@lib/$$1@g' < "$<" > "$@"
lib/.depend:
	make -C lib depend
include depfun.mak
include .libdepend

FORCE: ;
