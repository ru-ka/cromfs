src_dir=/mnt/rom-images/snes-src
export TEMP=/mnt/cromtmp

~/src/cromfs/util/mkcromfs $src_dir snes-1024a.cromfs \
	-er10000 -b1024 -f$[16*1024*1024] -c8 -A256 --threads 4 \
	--blockindexmethod prepass \
	-g16 \
	-3 -l \
 &> snes-1024a.log
