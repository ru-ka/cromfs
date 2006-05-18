<?php
require_once '/WWW/email.php';

//TITLE=Compressed ROM filesystem for Linux

$title = 'Cromfs: Compressed ROM filesystem for Linux (user-space)';
$progname = 'cromfs';

function usagetext($prog)
{
  exec('/usr/local/bin/'.$prog.' --help', $kk);
  $k='';foreach($kk as $s)$k.="$s\n";
  return $k;
}
 
$text = array(
   '1. Purpose' => "

Cromfs is a compressed read-only filesystem for Linux. Cromfs is intended
for permanently archiving gigabytes of big files that have lots of redundancy.
 <p/>
In terms of compression it is much similar to
<a href=\"http://www.7-zip.com/\">7-zip</a> files, except that fast random
access is provided  for the whole archive contents; the user does not need
to launch a program to decompress a single file, nor does he need to wait
while the system decompresses 500 files from a 1000-file archive to get
him the 1 file he wanted to open.
 <p/>
Note: The primary design goal of cromfs is compression power.
It is much slower than its peers, and uses more RAM.
If all you care about is \"powerful compression\"
and \"random file access\", then you will be happy with cromfs.
 <p/>
The creation of cromfs was inspired
from <a href=\"http://squashfs.sourceforge.net/\">Squashfs</a>
and <a href=\"http://sourceforge.net/projects/cramfs/\">Cramfs</a>.

", 'news:1. News' => "

See the <a href=\"http://bisqwit.iki.fi/src/cromfs-changelog.txt\">ChangeLog</a>.

", 'overview:1. Overview' => "

<ul>
 <li>Data, inodes, directories and block lists are stored compressed</li>
 <li>Duplicate inodes, files and even duplicate file portions are detected and stored only once
   <ul>
    <li>Especially suitable for gigabyte-class archives of
     thousands of nearly-identical megabyte-class files.</li>
   </ul>
  </li>
 <li>Files are stored in solid blocks, meaning that parts of different
  files are compressed together for effective compression</li>
 <li>Most of inode types recognized by Linux are supported (see <a href=\"#compare\">comparisons</a>).</li>
 <li>The <a href=\"http://www.7-zip.com/sdk.html\">LZMA compression</a> is used.
  In the general case, LZMA compresses better than gzip and bzip2.</li>
 <li>As with usual filesystems, the files on a cromfs volume can be accessed
  in arbitrary order; the waits to open a specific file are small, despite
  the files being semisolidly archived.</li>
 <li>Works on 64-bit and 32-bit systems.</li>
</ul>

See <a href=\"http://bisqwit.iki.fi/src/cromfs-format.txt\"
>the documentation of the cromfs format</a> for technical details
(also included in the source package as doc/FORMAT).

", 'limits:1. Limitations' => "

<ul>
 <li>Filesystem is write-once, read-only. It is not possible to append
  to a previously-created filesystem, nor it is to mount it read-write.</li>
 <li>Max filesize: 2^64 bytes (16777216 TB), but 256 TB with default settings.</li>
 <li>Max number of files in a directory: 2^30 (smaller if filenames are longer, but still more than 100000 in almost all cases)</li>
 <li>Max number of inodes (all files, dirs etc combined): 2^60, but depends on file sizes</li>
 <li>Max filesystem size: 2^64 bytes (16777216 TB)</li>
 <li>There are no \".\" and \"..\" entries in directories.</li>
 <li>mkcromfs is slow. You must be patient.</li>
 <li>The cromfs-driver has a large memory footprint. It is not
   suitable for very size-constrained systems.</li>
 <li>Maximum filename length: 4095 bytes</li>
 <li>Being an user-space filesystem, it might not be suitable for
   root filesystems of rescue, tiny-Linux and installation disks.
   (Facts needed.)</li>
 <li>For device inodes, hardlink count of 1 is assumed.
   (This has no effect to compression efficiency.)</li>
</ul>

Development status: Beta. The Cromfs project has been created very recently
and it hasn't been yet tested extensively. There is no warranty against data
loss or anything else, so use at your own risk.<br />
That being said, there are no known bugs.

", 'compare:1. Comparing to other filesystems' => "

This is all very biased, hypothetical, and by no means
a scientific study, but here goes:

<style type=\"text/css\"><!--
.good  { background:#CFC }
.bad   { background:#FCC }
.hmm   { background:#FFC }
--></style>
<table border=1 class=fscom>
 <tr align=left>
  <th>Feature</th>
   <th>Cromfs</th>
   <th>Cramfs</th>
   <th>Squashfs (3.0)</th>
   <th>Cloop</th>
  </tr>
 <tr align=left>
  <th>Compression unit</th>
   <td class=good>adjustable arbitrarily (2 MB default)</td>
   <td class=hmm>adjustable, must be power of 2 (4 kB default)</td>
   <td class=hmm>adjustable, must be power of 2 (64 kB max)</td>
   <td class=hmm>adjustable in 512-byte units (1 MB max)</td>
  </tr>
 <tr align=left>
  <th>Files are compressed (up to block size limit)</th>
   <td class=good>Together</td>
   <td class=hmm>Individually</td>
   <td class=hmm>Individually, except for fragments</td>
   <td class=good>Together</td>
  </tr>
 <tr align=left>
  <th>Maximum file size</th>
   <td class=hmm>16 EB (2^44 MB) (theoretical; actual limit depends on settings)</td>
   <td class=bad>16 MB (2^4 MB)</td>
   <td class=good>16 EB (2^44 MB)<br /> (4 GB before v3.0)</td>
   <td class=good>Depends on slave filesystem</td>
  </tr>
 <tr align=left>
  <th>Maximum filesystem size</th>
   <td class=good>16 EB (2^44 MB)</td>
   <td class=bad>272 MB</td>
   <td class=good>16 EB (2^44 MB)<br /> (4 GB before v3.0)</td>
   <td>Unknown</td>
  </tr>
 <tr align=left>
  <th>Duplicate whole file detection</th>
   <td class=good>Yes</td>
   <td class=bad>No</td>
   <td class=good>Yes</td>
   <td class=bad>No</td>
  </tr>
 <tr align=left>
  <th>Hardlinks detected and saved</th>
   <td class=good>Yes</td>
   <td class=good>Yes</td>
   <td class=good>Yes, since v3.0</td>
   <td class=good>depends on slave filesystem</td>
  </tr>
 <tr align=left>
  <th>Near-identical file detection</th>
   <td class=good>Yes (identical blocks)</td>
   <td class=bad>No</td>
   <td class=bad>No</td>
   <td class=bad>No</td>
  </tr>
 <tr align=left>
  <th>Compression method</th>
   <td class=good>LZMA</td>
   <td class=hmm>gzip (patches exist to use LZMA)</td>
   <td class=hmm>gzip (patches exist to use LZMA)</td>
   <td class=good>gzip or LZMA</td>
  </tr>
 <tr align=left>
  <th>Ownerships</th>
   <td class=good>uid,gid (since version 1.1.2)</li>
   <td class=hmm>uid,gid (but gid truncated to 8 bits)</td>
   <td class=good>uid,gid</td>
   <td class=good>Depends on slave filesystem</td>
 <tr align=left>
  <th>Timestamps</th>
   <td class=hmm>mtime only</td>
   <td class=bad>None</td>
   <td class=hmm>mtime only</td>
   <td class=good>Depends on slave filesystem</td>
 <tr align=left>
  <th>Endianess-safety</th>
   <td class=bad>Works on little-endian only</td>
   <td class=hmm>Safe, but not exchangeable</td>
   <td class=hmm>Safe, but not exchangeable</td>
   <td class=hmm>Depends on slave filesystem</td>
 <tr align=left>
  <th>Kernelspace/userspace</th>
   <td>User (fuse)</td>
   <td>Kernel</td>
   <td>Kernel</td>
   <td>Kernel</td>
 <tr align=left>
  <th>Appending to a previously created filesystem</th>
   <td class=bad>No</td>
   <td class=bad>No</td>
   <td class=good>Yes</td>
   <td class=bad>No (the slave filesystem can
     be decompressed, modified, and compressed
     again, but in a sense, so can every other
     of these.)</td>
 <tr align=left>
  <th>Supported inode types</th>
   <td class=good>all</td>
   <td class=good>all</td>
   <td class=good>all</td>
   <td class=good>Depends on slave filesystem</td>
 <tr align=left>
  <th>Fragmentation<br />(good for compression, bad for access speed)</th>
   <td class=hmm>Commonplace</th>
   <td class=hmm>None</td>
   <td class=good>File tails only</td>
   <td class=hmm>Depends on slave filesystem</td>
 <tr align=left>
  <th>Holes (aka. sparse files); storage optimization
      of blocks which consist entirely of nul bytes</th>
   <td class=good>Optimized, not limited to nul-byte blocks.</th>
   <td class=good>Supported</td>
   <td class=bad>Not supported</th>
   <td class=good>Depends on slave filesystem</td>
 <tr align=left>
  <th>Waste space (partially filled sectors)</th>
   <td class=good>No</td>
   <td>Unknown</td>
   <td class=hmm>Mostly not</td>
   <td class=hmm>Depends on slave filesystem, generally yes</td>
</table>

Note: cromfs now saves the uid and gid in the filesystem. However,
when the uid is 0 (root), the cromfs-driver returns the uid of the
user who mounted the filesystem, instead of root. Similarly for gid.
This is both for backward compatibility and for security.<br />
If you mount as root, this behavior has no effect.

", 'compression:1.1. Compression tests' =>"

Note: I use the -e and -r options in all of these mkcromfs tests
to avoid unnecessary decompression+recompression steps, in order
to speed up the filesystem generation. This has no effect in
compression ratio.

<style type=\"text/css\"><!--
.comcom b  { color:#007 }
.comcom tt { display:block; width:100%; color:#050; background:#EEE }
--></style>
<table border=\"1\" style=\"font-size:12px\" class=\"comcom\">
 <tr>
  <th>Item</th>
  <th align=\"left\">10783 NES ROMs (2523 MB)</th>
  <th align=\"left\">Mozilla source code from CVS (279 MB)</th>
  <th align=\"left\">Damn small Linux liveCD (113 MB)<br />
   (size taken from \"du -c\" output in the uncompressed filesystem)</th>
 </tr>
 <tr align=\"right\"3 valign=\"top\">
  <th>Cromfs</th>
  <td class=good><tt>mkcromfs -s16384 -f16777216</tt>
   <br />With 2k blocks (-b2048 -a16), <b>202,811,971</b> bytes
   <br />With 1k blocks (-b1024 -a32), <b>198,410,407</b> bytes
   <!--<br />With 512B blocks (-b512 -a2), <b>194,795,834</b> bytes-->
   <br />With 256B blocks (-b256 -a4), <b>194,386,381</b> bytes
   </td>
  <td class=good><tt>mkcromfs -b65536 -f2097152</tt>
   <br /><b>29,525,376</b> bytes</td>
  <td class=good><tt>mkcromfs -f1048576</tt>
   <br />With 64k blocks (-b65536), <b>39,778,030</b> bytes
   <br />With 16k blocks (-b16384), <b>39,718,882</b> bytes
   <br />With 1k blocks (-b1024), <b>40,141,729</b> bytes
   </td>
 </tr>
 <tr align=\"right\" valign=\"top\">
  <th>Cramfs</th>
  <td class=bad><tt>mkcramfs -b65536</tt>
   <br />dies prematurely, \"filesystem too big\"</td>
  <td class=bad><tt>mkcramfs</tt>
   <br />with 2M blocks (-b2097152), <b>58,720,256</b> bytes
   <br />with 64k blocks (-b65536), <b>57,344,000</b> bytes
   <br />with 4k blocks (-b4096), <b>68,435,968</b> bytes
   </td>
  <td class=bad><tt>mkcramfs -b65536</tt>
   <br /><b>51,445,760</b> bytes
   </td>
 </tr>
 <tr align=\"right\" valign=\"top\">
  <th>Squashfs</th>
  <td class=bad><tt>mksquashfs -b65536</tt>
   <br />(using an optimized sort file) <b>1,185,546,240</b> bytes</td>
  <td class=hmm><tt>mksquashfs -b65536</tt>
   <br /><b>43,335,680</b> bytes</td>
  <td class=bad><tt>mksquashfs -b65536</tt>
   <br /><b>50,028,544</b> bytes
    </td>
 </tr>
 <tr align=\"right\" valign=\"top\">
  <th>Cloop</th>
  <td class=bad><tt>create_compressed_fs</tt>
   <br />(using an iso9660 image created with mkisofs -R)
   <br />using 7zip, 1M blocks (-B1048576 -t2 -L-1): <b>1,136,789,006</b> bytes
   </td>
  <td class=hmm><tt>create_compressed_fs</tt>
   <br />(using an iso9660 image created with mkisofs -RJ)
   <br />using 7zip, 1M blocks (-B1048576 -L-1): <b>41,201,014</b> bytes
    <br />(1 MB is maximum block size in cloop)
   </td>
  <td class=bad><tt>create_compressed_fs</tt>
   <br />(using an iso9660 image)
   <br />using 7zip, 1M blocks (-B1048576 -L-1): <b>48,328,580</b> bytes
   <br />using zlib, 64k blocks (-B65536 -L9): <b>50,641,093</b> bytes
   </td>
 </tr>
 <tr align=\"right\" valign=\"top\">
  <th>7-zip (p7zip)<br /> (an archive, not a filesystem)</th>
  <td><tt>7za -mx9 -ma=2 a</tt>
   <br /><b>235,037,017</b> bytes
  <td>untested</td>
  <td><tt>7za -mx9 -ma2 a</tt>
    <br /><b>37,205,238</b> bytes
   </td>
 </tr>
</table>

An explanation why mkcromfs beats 7-zip in the NES ROM packing test:
<blockquote style=\"font-size:92%;color:#222\">
 7-zip packs all the files together as one stream. The maximum dictionary
 size is 32 MB. When 32 MB of data has been packed and more data comes in,
 similarities between the first megabytes of data and the latest data are
 not utilized. For example, <i>Mega Man</i> and <i>Rockman</i> are two
 almost identical versions of the same image, but because there's more
 than 500 MB of files in between of those when they are processed in
 alphabetical order, 7-zip does not see that they are similar, and will
 compress each one separately.<br />
 7-zip's chances could be improved by sorting the files so that it will
 process similar images sequentially.<br />
<br />
 mkcromfs however keeps track of all blocks it has encoded, and will remember
 similarities no matter how long ago they were added to the archive. This is
 why it outperforms 7-zip in this case.<br />
<br />
 In the liveCD compressing test, mkcromfs does not beat 7-zip because this
 advantage is too minor to overcome the overhead needed to provide random
 access to the filesystem. It still beats cloop, squashfs and cramfs though.
</blockquote>

", 'speed:1.1. Speed tests' => "

Speed testing hasn't been done yet. It is difficult to test the speed,
because it depends on factors such as cache (with compressed filesystems,
decompression consumes CPU power but usually only needs to be done once)
and block size (bigger blocks need more time to decompress).
 <p />
However, in the general case, it is quite safe to assume
that mkcromfs is the <i>slowest</i> of all. The same goes
for resource testing (RAM).

", 'usage:1. Getting started' => "

<ol>
 <li>Install the development requirements: make, gcc-c++ and fuse
  <ul>
   <li>Remember that for fuse to work, the kernel must also contain the fuse support.
    Do \"modprobe fuse\", and check if you have \"/dev/fuse\" and check if it works.
    <ul><li>If an attempt to read from \"/dev/fuse\" (as root) gives \"no such device\",
    it does not work. If it gives \"operation not permitted\", it might work.</li></ul></li>
  </ul></li>
 <li>Build \"cromfs-driver\" and \"util/mkcromfs\", i.e. command \"make\":
  <pre>\$ make</pre>
  
  If you get compilation problems related to <tt>hash_map</tt> or <tt>hash</tt>, 
  edit cromfs-defs.hh and remove the line that says <tt>#define USE_HASHMAP</tt>.
   </li>
 <li>Create a sample filesystem:
  <pre>\$ util/mkcromfs . sample.cromfs</pre>
   </li>
 <li>Mount the sample filesystem:
  <pre>\$ mkdir sample
\$ ./cromfs-driver sample.cromfs sample &</pre>
   </li>
 <li>Observe the sample filesystem:
  <pre>\$ cd sample
\$ ls -al</pre>
   </li>
 <li>Unmounting the filesystem:
  <pre>\$ cd ..
\$ fusermount -u sample</pre>
    or, type \"fg\" and press ctrl-c to terminate the driver.
   </li>
</ol>

", 'tips:1. Tips' => "

To improve the compression, try these tips:
<ul>
 <li>Adjust the block size (--bsize) in mkcromfs. If your files
     have a lot identical content, aligned at a certain boundary,
     use that boundary as the block size value. If you are uncertain,
     use a small value (500-5000) rather than a bigger value (20000-400000).
     Too small values will however make inodes large, so keep it sane.
    <br />
     Note: The value does not need to be a power of two.
  </li>
 <li>Adjust the fblock size (--fsize) in mkcromfs. Larger values
     cause almost always better compression.
    <br />
     Note: The value does not need to be a power of two.
  </li>
 <li>Adjust the --autoindexratio option (-a). A bigger value will
     increase the chances of mkcromfs finding an identical block
     from something it already processed (if your data has that
     opportunity). Finding that two blocks are identical always
     means better compression.</li>
 <li>Sort your files. Files which have similar or partially
     identical content should be processed right after one other.</li>
 <li>Adjust the --bruteforcelimit option (-c). Larger values will require
     mkcromfs to check more fblocks for each block it encodes (making the
     encoding much slower), in the hope it improves compression.
     The fewer your fblocks are in number (larger in size),
     the better the chances it does good.
    <br />
     Note: If you use --bruteforcelimit, you should also adjust
     your --minfreespace setting as instructed in <tt>mkcromfs --help</tt>.
  </li>
</ul>
To improve the filesystem generation speed, try these tips:
<ul>
 <li>Use the --decompresslookups option (-e), if you have the
     diskspace to spare.</li>
 <li>Use the TEMP environment variable to control where the temp
     files are written. Example: <tt>TEMP=~/cromfs-temp ./mkcromfs ...</tt></li>
 <li>Use larger block size (--bsize). Smaller blocks mean more blocks
     which means more work. Larger blocks are less work.</li>
 <li>Do not use the --bruteforcelimit option (-c). The default value 0
     means that only one fblock will be assumed as a candidate.</li>
</ul>
To control the memory usage, use these tips:
<ul>
 <li>Adjust the fblock size (--fsize). The memory used by cromfs-driver
     is directly proportional to the size of your fblocks. It keeps at
     most 10 fblocks decompressed in the RAM at a time. If your fblocks
     are 4 MB in size, it will use 40 MB at max.</li>
 <li>In mkcromfs, adjust the --autoindexratio option (-a). This will
     not have effect on the memory usage of cromfs-driver, but it will
     control the memory usage of mkcromfs. If you have lots of RAM, you
     should use bigger --autoindexratio (because it will improve the chances
     of getting better compression results), and use smaller if you have less RAM.</li>
 <li>Find the CACHE_MAX_SIZE settings in cromfs.cc and edit them. This will
     require recompiling the source. (In future, this should be made a command
     line option for cromfs-driver.)</li>
</ul>
To control the filesystem speed, use these tips:
<ul>
 <li>The speed of the underlying storage affects.</li>
 <li>The bigger your fblocks (--fsize), the bigger the latencies are.
   cromfs-driver caches the decompressed fblocks, but opening a non-cached
   fblock requires decompressing it entirely, which will block the user
   process for that period of time.</li>
 <li>The smaller your blocks (--bsize), the bigger the latencies are, because
   there will be more steps to process for handling the same amount of data.</li>
 <li>Use the most powerful compiler and compiler settings available
   for building cromfs-driver. This helps the decompression and cache lookups.</li>
 <li>Use fast hardware&hellip;</li>
</ul>

", 'copying:1. Copying' => "

cromfs has been written by Joel Yliluoma, a.k.a.
<a href=\"http://iki.fi/bisqwit/\">Bisqwit</a>,<br>
and is distributed under the terms of the
<a href=\"http://www.gnu.org/licenses/licenses.html#GPL\">General Public License</a> (GPL).
 <br/>
The LZMA code embedded within is licensed under LGPL.
 <p/>
Patches and other related material can be submitted
".GetEmail('by e-mail at:', 'Joel Yliluoma', 'bisqwi'. 't@iki.fi')."

", 'requ:1. Requirements' => "

<ul>
 <li>GNU make and gcc-c++ are required to recompile the source code.</li>
 <li>The filesystem works under the <a href=\"http://fuse.sourceforge.net/\">Fuse</a>
  user-space filesystem framework. You need to install both the Fuse kernel
  module and the userspace programs before mounting Cromfs volumes.<br />
  You need version fuse version 2.6.0 or newer. (2.5.2 <i>might</i> work.)</li>
</ul>


");
include '/WWW/progdesc.php';
