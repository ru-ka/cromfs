src_dir=/mnt/rom-images/spc-src
export TEMP=/mnt/cromtmp

~/src/cromfs/util/mkcromfs $src_dir spc-256d.cromfs \
	-er15000000 -b4096 -f$[4*1024*1024] -c1 -A256 --threads 4 \
	--blockindexmethod prepass \
	-g16 \
 &> spc-256d.log
