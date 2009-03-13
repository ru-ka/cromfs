#!/bin/sh

# - Symlinks are not tested here, because they fail date comparison
#   (symlink timestamps cannot be set)
# 
# - Device entries are not tested here, because they cannot be extracted
#   without being root
#

## TEST 1

if true; then
	make -C ../util mkcromfs unmkcromfs -j4
	rm -f tmp.cromfs
	echo "Packing..."
	#valgrind --leak-check=full \
	
	../util/mkcromfs a tmp.cromfs -B'*.hh:128' -b16384 -f65536 -e --threads 1 >/dev/null
	rm -rf b
	echo "Unpacking..."
	#valgrind --leak-check=full \
	
	../util/unmkcromfs tmp.cromfs b >/dev/null

	( cd a && tar cf - *) | tar tvvf - | sort > a.listing
	( cd b && tar cf - *) | tar tvvf - | sort > b.listing

	if diff -u a.listing b.listing; then
		echo "*** TEST 1: PASS"
	else
		echo "*** TEST 1: FAIL"
	fi
	rm -rf a.listing b.listing b tmp.cromfs
fi

## TEST 2 (no sparse files)

if true; then
	make -C ../util mkcromfs unmkcromfs -j4
	rm -f tmp.cromfs
	echo "Packing..."
	#valgrind --leak-check=full \
	
	../util/mkcromfs a tmp.cromfs -b64 -f512 --threads 20 --blockindexmethod prepass -A1 >/dev/null
	rm -rf b
	echo "Unpacking..."
	#valgrind --leak-check=full \
	
	../util/unmkcromfs tmp.cromfs b -s >/dev/null

	( cd a && tar cf - *) | tar tvvf - | sort > a.listing
	( cd b && tar cf - *) | tar tvvf - | sort > b.listing

	rm -rf b tmp.cromfs

	if diff -u a.listing b.listing; then
		echo "*** TEST 2: PASS"
	else
		echo "*** TEST 2: FAIL"
	fi
	rm -rf a.listing b.listing tmp.cromfs b
fi

if [ "$CXX" = "" ]; then CXX=g++; fi

## TEST 3: Boyer-Moore
if true; then
	$CXX -o test-boyermoore -O3 test-boyermoore.cc -ftree-vectorize
	echo "Testing Boyer-Moore search algorithm..."
	./test-boyermoore
	rm -f test-boyermoore
fi

## TEST 4: Backwards match
if true; then
	$CXX -o test-backwards_match -O3 test-backwards_match.cc -g -Wall -W -ftree-vectorize
	echo "Testing Backwards match..."
	./test-backwards_match
	rm -f test-backwards_match
fi

## TEST 5: Hashmaps
if false; then
	$CXX -o test-hashmaps -O3 test-hashmaps.cc \
		../lib/assert++.cc \
		../lib/newhash.cc -g -Wall -W -ftree-vectorize \
		-I../lib -I../lib/lzo -DHAS_LZO2=1 -DHAS_ASM_LZO2=0
	echo "Testing Hashmaps..."
	./test-hashmaps
	rm -f test-hashmaps
fi
