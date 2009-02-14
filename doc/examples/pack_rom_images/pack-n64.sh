src_dir=/mnt/rom-images/n64-src
export TEMP=/mnt/cromtmp

~/src/cromfs/util/mkcromfs $src_dir n64-2048a.cromfs \
	-er15000 -b2048 -f$[16*1024*1024] -c16 -A2048 --threads 3 \
	--blockindexmethod prepass \
	-g256 \
	-l \
 &> n64-2048a.log
