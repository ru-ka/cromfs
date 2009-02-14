src_dir=/mnt/rom-images/gens-copy

export TEMP=/mnt/cromtmp

~/src/cromfs/util/mkcromfs $src_dir gens-256a.cromfs \
	-er1500000 -b256 -f$[16*1024*1024] -c16 -A64 --threads 2 \
	--blockindexmethod prepass \
	-g16 \
	-3 -l \
 &> gens-256a.log
