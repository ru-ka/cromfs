src_dir=/mnt/rom-images/sms-src
export TEMP=/mnt/cromtmp

~/src/cromfs/util/mkcromfs $src_dir sms-128a.cromfs \
	-er1500000 -b128 -f$[8*1024*1024] -c16 -A32 --threads 3 \
	--blockindexmethod prepass \
	-g16 \
	-l \
	--resume-blockify "$TEMP/sms-resume-" \
 &> sms-128a.log
