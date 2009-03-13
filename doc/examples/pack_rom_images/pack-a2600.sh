src_dir=/mnt/rom-images/a2600-src
export TEMP=/mnt/cromtmp

~/src/cromfs/util/mkcromfs $src_dir a2600-128i.cromfs \
	-er1500000 -b128 -f$[8*1024*1024] -c64 -A2 --threads 2 \
	--blockindexmethod prepass \
	-g128 \
	-3 -l \
 &> a2600-128i.log
