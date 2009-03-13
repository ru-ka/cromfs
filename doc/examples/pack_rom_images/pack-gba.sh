src_dir=/mnt/rom-images/gba-src
export TEMP=/mnt/cromtmp

~/src/cromfs/util/mkcromfs $src_dir gba-1024e.cromfs \
	-er1500000000 -b128 -f$[8*1024*1024] -c16 -A8192 --threads 4 \
	--blockindexmethod prepass \
	-g128 \
	--nosortbyfilename \
	--resume-blockify "$TEMP/resume-" \
	\
	-B'*.gba:1024' \
	-B'*/PD/*:16384' \
	-B'INOTAB:1024' \
	-B'*.gb:256' \
	-B'*.gbc:256' \
	-B'*/Goomba*:256' \
	-B'*.nes:128' \
	-B'*/PocketNES*:128' \
	-B'*/NES on GBA*:128' \
	-B'*.sms:128' \
	-B'*.sc:128' \
	-B'*.sf7:128' \
	-B'*.sg:128' \
	-B'*/DrSMS*:128' \
	-B'*/SMS*:128' \
	-B'*/PocketSMS*:128' \
	-B'*/Video/*:65536' \
 &> gba-1024e.log
