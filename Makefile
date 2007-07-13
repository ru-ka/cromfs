VERSION=1.4.0
ARCHNAME=cromfs-$(VERSION)

ARCHDIR=archives/
ARCHFILES=\
	Makefile.sets \
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
	cromfs-ops.cc cromfs-ops.hh \
	main.c \
	LzmaDecode.c LzmaDecode.h LzmaTypes.h \
	\
	range.hh range.tcc \
	rangeset.hh rangeset.tcc \
	rangemultimap.hh rangemultimap.tcc \
	\
	util/Makefile.sets util/Makefile util/depfun.mak \
	\
	hash.hh \
	\
	util/datasource.hh \
	util/lzma.cc util/lzma.hh \
	util/util.cc util/util.hh \
	util/fblock.cc util/fblock.hh \
	util/append.cc util/append.hh \
	util/fnmatch.cc util/fnmatch.hh \
	util/crc32.h util/crc32.cc \
	util/assert++.hh util/assert++.cc \
	util/memmem.h util/memmem.c \
	util/boyermoore.hh \
	util/mkcromfs.cc \
	util/unmkcromfs.cc \
	util/cvcromfs.cc \
	util/mkcromfs_sets.hh \
	\
	util/autoptr \
	util/range.hh util/range.tcc \
	util/rangeset.hh util/rangeset.tcc \
	util/rangemultimap.hh util/rangemultimap.tcc \
	\
	util/lzma/ORIGIN \
	util/lzma/LGPL.txt \
	util/lzma/lzma.txt \
	\
	util/lzma/C/7zCrc.c \
	util/lzma/C/7zCrc.h \
	util/lzma/C/Alloc.c \
	util/lzma/C/Alloc.h \
	util/lzma/C/IStream.h \
	util/lzma/C/Types.h \
	util/lzma/CPP/Common/CRC.cpp \
	util/lzma/CPP/Common/Defs.h \
	util/lzma/CPP/Common/MyCom.h \
	util/lzma/CPP/Common/MyException.h \
	util/lzma/CPP/Common/MyGuidDef.h \
	util/lzma/CPP/Common/MyInitGuid.h \
	util/lzma/CPP/Common/MyUnknown.h \
	util/lzma/CPP/Common/MyWindows.h \
	util/lzma/CPP/Common/NewHandler.h \
	util/lzma/CPP/Common/StdAfx.h \
	util/lzma/CPP/Common/Types.h \
	util/lzma/C/Compress/Lz/LzHash.h \
	util/lzma/C/Compress/Lz/MatchFinder.c \
	util/lzma/C/Compress/Lz/MatchFinder.h \
	util/lzma/C/Compress/Lzma/LzmaDecode.c \
	util/lzma/C/Compress/Lzma/LzmaDecode.h \
	util/lzma/C/Compress/Lzma/LzmaTypes.h \
	util/lzma/CPP/7zip/ICoder.h \
	util/lzma/CPP/7zip/IStream.h \
	util/lzma/CPP/7zip/Common/InBuffer.h \
	util/lzma/CPP/7zip/Common/OutBuffer.cpp \
	util/lzma/CPP/7zip/Common/OutBuffer.h \
	util/lzma/CPP/7zip/Common/StdAfx.h \
	util/lzma/CPP/7zip/Common/StreamUtils.cpp \
	util/lzma/CPP/7zip/Common/StreamUtils.h \
	util/lzma/CPP/7zip/Compress/LZMA/LZMAEncoder.cpp \
	util/lzma/CPP/7zip/Compress/LZMA/LZMAEncoder.h \
	util/lzma/CPP/7zip/Compress/LZMA/LZMA.h \
	util/lzma/CPP/7zip/Compress/LZMA/StdAfx.h \
	util/lzma/CPP/7zip/Compress/RangeCoder/RangeCoderBit.cpp \
	util/lzma/CPP/7zip/Compress/RangeCoder/RangeCoderBit.h \
	util/lzma/CPP/7zip/Compress/RangeCoder/RangeCoderBitTree.h \
	util/lzma/CPP/7zip/Compress/RangeCoder/RangeCoder.h \
	util/lzma/CPP/7zip/Compress/RangeCoder/RangeCoderOpt.h \
	util/lzma/CPP/7zip/Compress/RangeCoder/StdAfx.h \
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

OBJS=cromfs.o cromfs-ops.o main.o

LDLIBS += -lfuse

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
	$(CXX) $(CXXFLAGS) -o $@ $(OBJS) $(LDLIBS)

cromfs-driver-static: $(OBJS)
	$(CXX) -static $(CXXFLAGS) -o $@ $(OBJS) $(LDLIBS) -lpthread -lrt
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

include depfun.mak

FORCE: ;
