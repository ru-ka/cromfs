VERSION=1.2.4.1
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
	doc/boxart.png \
	cromfs.spec \
	\
	cromfs.cc cromfs.hh cromfs-defs.hh \
	cromfs-ops.cc cromfs-ops.hh \
	main.c \
	LzmaDecode.c LzmaDecode.h \
	\
	dummy_thread.c \
	\
	util/Makefile.sets util/Makefile util/depfun.mak \
	\
	util/datasource.hh \
	util/lzma.cc util/lzma.hh \
	util/util.cc util/util.hh \
	util/fblock.cc util/fblock.hh \
	util/fnmatch.cc util/fnmatch.hh \
	util/crc32.h util/crc32.cc \
	util/memmem.h util/memmem.c \
	util/boyermoore.hh \
	util/mkcromfs.cc \
	util/unmkcromfs.cc \
	util/cvcromfs.cc \
	util/mkcromfs_sets.hh \
	\
	util/range.hh util/range.tcc \
	util/rangeset.hh util/rangeset.tcc \
	util/rangemultimap.hh util/rangemultimap.tcc \
	\
	util/lzma/ORIGIN \
	util/lzma/LGPL.txt \
	\
	util/lzma/C/7zip/Common/OutBuffer.h \
	util/lzma/C/7zip/Common/StreamUtils.cpp \
	util/lzma/C/7zip/Common/StdAfx.h \
	util/lzma/C/7zip/Common/StreamUtils.h \
	util/lzma/C/7zip/Common/InBuffer.h \
	util/lzma/C/7zip/Common/OutBuffer.cpp \
	util/lzma/C/7zip/Compress/LZ/HashChain/HC4.h \
	util/lzma/C/7zip/Compress/LZ/HashChain/HCMain.h \
	util/lzma/C/7zip/Compress/LZ/BinTree/BinTree2.h \
	util/lzma/C/7zip/Compress/LZ/BinTree/BinTree3.h \
	util/lzma/C/7zip/Compress/LZ/BinTree/BinTree4.h \
	util/lzma/C/7zip/Compress/LZ/BinTree/BinTreeMain.h \
	util/lzma/C/7zip/Compress/LZ/BinTree/BinTree.h \
	util/lzma/C/7zip/Compress/LZ/IMatchFinder.h \
	util/lzma/C/7zip/Compress/LZ/StdAfx.h \
	util/lzma/C/7zip/Compress/LZ/LZInWindow.cpp \
	util/lzma/C/7zip/Compress/LZ/LZInWindow.h \
	util/lzma/C/7zip/Compress/LZMA/LZMA.h \
	util/lzma/C/7zip/Compress/LZMA/StdAfx.h \
	util/lzma/C/7zip/Compress/LZMA/LZMAEncoder.h \
	util/lzma/C/7zip/Compress/LZMA/LZMAEncoder.cpp \
	util/lzma/C/7zip/Compress/RangeCoder/RangeCoderBit.cpp \
	util/lzma/C/7zip/Compress/RangeCoder/StdAfx.h \
	util/lzma/C/7zip/Compress/RangeCoder/RangeCoder.h \
	util/lzma/C/7zip/Compress/RangeCoder/RangeCoderBit.h \
	util/lzma/C/7zip/Compress/RangeCoder/RangeCoderOpt.h \
	util/lzma/C/7zip/Compress/RangeCoder/RangeCoderBitTree.h \
	util/lzma/C/7zip/ICoder.h \
	util/lzma/C/7zip/IStream.h \
	util/lzma/C/Common/Defs.h \
	util/lzma/C/Common/Alloc.h \
	util/lzma/C/Common/CRC.h \
	util/lzma/C/Common/MyUnknown.h \
	util/lzma/C/Common/StdAfx.h \
	util/lzma/C/Common/MyWindows.h \
	util/lzma/C/Common/MyInitGuid.h \
	util/lzma/C/Common/Alloc.cpp \
	util/lzma/C/Common/NewHandler.h \
	util/lzma/C/Common/MyGuidDef.h \
	util/lzma/C/Common/MyCom.h \
	util/lzma/C/Common/CRC.cpp \
	util/lzma/C/Common/Types.h \
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

all: $(PROGS)

cromfs-driver: $(OBJS)
	$(CXX) $(CXXFLAGS) -o $@ $(OBJS) $(LDLIBS)

cromfs-driver-static: $(OBJS)
	$(CXX) -static $(CXXFLAGS) -o $@ $(OBJS) $(LDLIBS) -lpthread -lrt
	- strip -R.comment $@
	- upx --best $@

#dummy_thread.o: dummy_thread.c
#	$(CC) -o $@ $^ -fomit-frame-pointer -c
	
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
