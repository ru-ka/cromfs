VERSION=1.0.6
ARCHNAME=cromfs-$(VERSION)

ARCHDIR=archives/
ARCHFILES=\
	Makefile.sets \
	\
	COPYING progdesc.php \
	\
	doc/FORMAT doc/README.html doc/ChangeLog \
	\
	cromfs.cc cromfs.hh cromfs-defs.hh \
	cromfs-ops.cc cromfs-ops.hh \
	main.c \
	LzmaDecode.c LzmaDecode.h \
	\
	util/Makefile.sets util/Makefile util/depfun.mak \
	\
	util/datasource.hh \
	util/lzma.cc util/lzma.hh \
	util/md5.hh util/main.cc \
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
	util/lzma/C/Common/Types.h

include Makefile.sets

CPPFLAGS += `pkg-config --cflags fuse`
OBJS=cromfs.o cromfs-ops.o main.o
LDLIBS += -lfuse

all: cromfs-driver util/mkcromfs

cromfs-driver: $(OBJS)
	$(CXX) $(CXXFLAGS) -o $@ $(OBJS) $(LDLIBS)


util/mkcromfs: FORCE
	make -C util mkcromfs

include depfun.mak

FORCE: ;
