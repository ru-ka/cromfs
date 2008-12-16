VERSION=1.5.6.1

include Makefile.sets

#OPTIM += -O0 -fno-inline -g
# -fmudflap
#LDOPTS += -fmudflap -lmudflap

OPTIM += -O3
#OPTIM += -O1 -g -fno-inline
CPPFLAGS += -pthread -I..
#OPTIM += -O1 -g
# -ftree-vectorize 

#OPTIM += -O1 -pg -g
#LDOPTS += -pg

# multithread versions of lzma -- Windows only
#CPPFLAGS += -DCOMPRESS_MF_MT
#CPPFLAGS += -DCOMPRESS_MT

include depfun.mak

clean:
	find -type d | while read d;do rm -f "$$d"/*.o;done
