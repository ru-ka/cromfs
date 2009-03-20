VERSION=1.5.8.4
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
	doc/SoftwareArchitecture.txt \
	doc/UnderstandingBlocksAndFblocks.txt \
	doc/WritingFrontends.txt \
	doc/WriteAccess.txt \
	doc/BlockIndexing.txt \
	doc/size_demo.txt \
	doc/boxart.png \
	doc/boxart-src.zip \
	cromfs.spec \
	\
	cromfs.cc cromfs.hh cromfs-defs.hh \
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
	lib/cromfs-blockindex.tcc lib/cromfs-blockindex.hh \
	lib/cromfs-blockifier.cc lib/cromfs-blockifier.hh \
	\
	lib/endian.hh \
	lib/simd.hh \
	lib/threadfun.hh \
	lib/threadfun_none.hh \
	lib/threadfun_openmp.hh \
	lib/threadfun_pthread.hh \
	lib/threadworkengine.hh lib/threadworkengine.tcc \
	lib/duffsdevice.hh \
	lib/stringsearchutil.cc \
	lib/stringsearchutil.hh \
	lib/stringsearchutil_backwardsmatch.tcc \
	lib/datasource.hh \
	lib/datareadbuf.hh \
	lib/datacache.hh \
	lib/mmapping.hh \
	lib/fadvise.cc lib/fadvise.hh \
	lib/lzma.cc lib/lzma.hh \
	lib/util.cc lib/util.hh \
	lib/append.cc lib/append.hh \
	lib/fnmatch.cc lib/fnmatch.hh \
	lib/newhash.h lib/newhash.cc \
	lib/assert++.hh lib/assert++.cc \
	lib/sparsewrite.cc lib/sparsewrite.hh \
	lib/longfileread.hh \
	lib/longfilewrite.hh lib/longfilewrite.cc \
	lib/mmap_vector.hh \
	lib/boyermooreneedle.hh \
	lib/boyermoore.hh lib/boyermoore.cc \
	lib/fsballocator.hh \
	lib/autodealloc.hh \
	lib/autoptr \
	\
	lib/allocatornk.hh \
	lib/rbtree.hh \
	\
	lib/range.hh lib/range.tcc \
	lib/rangeset.hh lib/rangeset.tcc \
	lib/rangemultimap.hh lib/rangemultimap.tcc \
	\
	lib/cromfs-hashmap_lzo.hh \
	lib/cromfs-hashmap_lzo.cc \
	lib/cromfs-hashmap_sparsefile.hh \
	lib/cromfs-hashmap_sparsefile.cc \
	lib/cromfs-hashmap_googlesparse.hh \
	lib/cromfs-hashmap_googlesparse.cc \
	lib/cromfs-hashmap_lzo_sparse.hh \
	lib/cromfs-hashmap_lzo_sparse.cc \
	lib/cromfs-hashmap_trivial.hh \
	\
	lib/google/dense_hash_map \
	lib/google/dense_hash_set \
	lib/google/sparse_hash_map \
	lib/google/sparse_hash_set \
	lib/google/sparsehash/densehashtable.h \
	lib/google/sparsehash/sparseconfig.h \
	lib/google/sparsehash/sparsehashtable.h \
	lib/google/sparsetable \
	lib/google/type_traits.h \
	\
	lib/lzo/config1x.h \
	lib/lzo/lzo1_d.ch \
	lib/lzo/lzo1x_1o.c \
	lib/lzo/lzo1x_c.ch \
	lib/lzo/lzo1x_d1.c \
	lib/lzo/lzo1x_d.ch \
	lib/lzo/lzo1x.h \
	lib/lzo/lzo_conf.h \
	lib/lzo/lzoconf.h \
	lib/lzo/lzodefs.h \
	lib/lzo/lzo_dict.h \
	lib/lzo/lzo_ptr.h \
	lib/lzo/miniacc.h \
	\
	lib/lzma/C/LzmaDec.c \
	lib/lzma/C/LzmaDec.h \
	lib/lzma/C/LzmaEnc.c \
	lib/lzma/C/LzmaEnc.h \
	lib/lzma/C/Types.h \
	lib/lzma/C/LzFind.c \
	lib/lzma/C/LzFind.h \
	lib/lzma/C/LzHash.h \
	lib/lzma/ORIGIN \
	lib/lzma/lzma.txt \
	\
	tests/run.sh \
	tests/a/fblock.hh tests/a/lzma.hh \
	tests/a/md5-copy.hh tests/a/md5.hh \
	tests/a/util.hh \
	tests/a/fifo tests/a/fifo-copy \
	tests/a/dir2 tests/a/dir2/util.cc \
	tests/a/dir2/util.hh \
	tests/a/sparse.bin \
	tests/test-boyermoore.cc \
	tests/test-hashmaps.cc \
	tests/test-backwards_match.cc \
	\
	doc/examples/pack_rom_images/README \
	doc/examples/pack_rom_images/make-spc-set-dir.sh \
	doc/examples/pack_rom_images/pack-a2600.sh \
	doc/examples/pack_rom_images/pack-gb.sh \
	doc/examples/pack_rom_images/pack-gba.sh \
	doc/examples/pack_rom_images/pack-gens.sh \
	doc/examples/pack_rom_images/pack-n64.sh \
	doc/examples/pack_rom_images/pack-nes.sh \
	doc/examples/pack_rom_images/pack-sms.sh \
	doc/examples/pack_rom_images/pack-snes.sh \
	doc/examples/pack_rom_images/pack-spc.sh \
	doc/examples/pack_rom_images/reconstruct-interrupted.sh \
	doc/examples/progress_monitor/Makefile \
	doc/examples/progress_monitor/README \
	doc/examples/progress_monitor/main.cc \
	doc/examples/progress_monitor/run-lsof.sh

include Makefile.sets

#CXXFLAGS += -O1 -fno-inline -g
CXXFLAGS += -O3 -fno-rtti

CPPFLAGS += `pkg-config --cflags fuse`
LDLIBS   += `pkg-config --libs fuse`

OBJS=\
	cromfs.o fuse-ops.o fuse-main.o \
	lib/cromfs-inodefun.o \
	lib/fadvise.o lib/util.o \
	lib/lzma/C/LzmaDec.o

LDLIBS += $(FUSELIBS)

DEPFUN_INSTALL=ignore

PROGS = cromfs-driver cromfs-driver-static-$(FUSE_STATIC) util/mkcromfs util/unmkcromfs util/cvcromfs
DOCS  = doc/FORMAT README.html doc/ChangeLog doc/*.txt

all-strip: all FORCE
	- strip cromfs-driver util/mkcromfs util/unmkcromfs util/cvcromfs
	@echo
	@echo Finished compiling. These were created:
	@- ls -al cromfs-driver cromfs-driver-static util/mkcromfs util/unmkcromfs util/cvcromfs

all: $(PROGS)

cromfs-driver: $(OBJS)
	$(CXX) $(CXXFLAGS) -o $@ $(OBJS) $(LDFLAGS) $(LDLIBS)

cromfs-driver-static-1: cromfs-driver-static
cromfs-driver-static-0: ;

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

clean: FORCE
	rm -rf $(OBJS) $(PROGS) install *.pchi
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

test: FORCE
	cd tests && ./run.sh

CTAGS=ctags --extra=+q
tags: FORCE
	(cd doc; $(CTAGS) ..{,/util,/lib{,/*,/lzma/*}}/{autoptr,*.{c,h,cc,hh,tcc,ch}})

.libdepend: lib/.depend
	perl -pe 's@([-+a-zA-Z0-9._/]+)@lib/$$1@g' < "$<" > "$@"
lib/.depend:
	make -C lib depend
include depfun.mak
include .libdepend

FORCE: ;
