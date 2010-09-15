VERSION=1.5.9

include Makefile.sets

#OPTIM += -O0 -fno-inline -g
# -fmudflap
#LDOPTS += -fmudflap -lmudflap

OPTIM += -O3
#OPTIM += -O1 -g -fno-inline
CPPFLAGS += -I.. -I../lib -I../lib/lzo
#OPTIM += -O1 -g
# -ftree-vectorize 

#OPTIM += -O1 -pg -g
#LDOPTS += -pg

# singlethreaded version of lzma
CPPFLAGS += -D_7ZIP_ST

tags: FORCE
	+make -C .. tags

include depfun.mak

clean: FORCE
	find -type d | while read d;do rm -f  "$$d"/*.pchi "$$d"/*.o;done

FORCE: ;
