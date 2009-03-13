src_dir=/mnt/rom-images/nes-orig
export TEMP=/mnt/cromtmp

~/src/cromfs/util/mkcromfs $src_dir nes-128c.cromfs \
	-er1500000 -b128 -f$[8*1024*1024] -c1 -A4096 --threads 4 \
	--blockindexmethod prepass \
	-g128 \
	-3 -l \
 &> nes-128d.log
