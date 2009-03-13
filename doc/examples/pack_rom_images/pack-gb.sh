src_dir=/mnt/rom-images/gb-src

export TEMP=/mnt/cromtmp

~/src/cromfs/util/mkcromfs $src_dir gb-128a.cromfs \
	-er1500000 -b128 -f$[16*1024*1024] -c16 -A256 --threads 3 \
	--blockindexmethod prepass \
	-g128 \
	-3 -l \
 &> gb-128a.log
