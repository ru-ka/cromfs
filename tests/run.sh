[A#!/bin/sh

# - Symlinks are not tested here, because they fail date comparison
#   (symlink timestamps cannot be set)
# 
# - Device entries are not tested here, because they cannot be extracted
#   without being root
#

make -C ../util mkcromfs unmkcromfs

## TEST 1

rm -f tmp.cromfs
../util/mkcromfs a tmp.cromfs -b16384 -f65536 -e  >/dev/null
rm -rf b
../util/unmkcromfs tmp.cromfs b >/dev/null

( cd a && tar cf - *) | tar tvvf - | sort > a.listing
( cd b && tar cf - *) | tar tvvf - | sort > b.listing

if diff -u a.listing b.listing; then
	echo "*** TEST 1: PASS"
else
	echo "*** TEST 1: FAIL"
fi
rm -rf a.listing b.listing b tmp.cromfs


## TEST 2 (no sparse files)

rm -f tmp.cromfs
../util/mkcromfs a tmp.cromfs -b64 -f512 > /dev/null
rm -rf b
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

if [ "$CXX" = "" ]; then CXX=g++; fi
$CXX -o test-boyermoore -O3 test-boyermoore.cc -ftree-vectorize
echo "Testing Boyer-Moore search algorithm..."
./test-boyermoore
rm -f test-boyermoore
