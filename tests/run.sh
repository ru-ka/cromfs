#!/bin/sh

# - Symlinks are not tested here, because they fail date comparison
#   (symlink timestamps cannot be set)
# 
# - Device entries are not tested here, because they cannot be extracted
#   without being root
#

make -C ../util mkcromfs unmkcromfs

## TEST 1

../util/mkcromfs a tmp.bin -b16384 -f65536 -e > /dev/null
rm -rf b
../util/unmkcromfs tmp.bin b >/dev/null

( cd a && tar cf - *) | tar tvvf - | sort > a.listing
( cd b && tar cf - *) | tar tvvf - | sort > b.listing

if diff -u a.listing b.listing; then
	echo "*** TEST 1: PASS"
else
	echo "*** TEST 1: FAIL"
fi
rm -f a.listing b.listing


## TEST 2 (no sparse files)

../util/mkcromfs a tmp.bin -b64 -f512 > /dev/null
rm -rf b
../util/unmkcromfs tmp.bin b -s >/dev/null

( cd a && tar cf - *) | tar tvvf - | sort > a.listing
( cd b && tar cf - *) | tar tvvf - | sort > b.listing

rm -rf b tmp.bin

if diff -u a.listing b.listing; then
	echo "*** TEST 2: PASS"
else
	echo "*** TEST 2: FAIL"
fi
rm -rf a.listing b.listing tmp.bin b
