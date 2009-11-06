<?php
require_once '/WWW/email.php';

//TITLE=Compressed ROM filesystem for Linux

$title = 'Cromfs: Compressed ROM filesystem for Linux (user-space)';
$progname = 'cromfs';
$git = 'git://bisqwit.iki.fi/cromfs.git';

function usagetext($prog)
{
  exec('/usr/local/bin/'.$prog.' --help', $kk);
  $k='';foreach($kk as $s)$k.="$s\n";
  return $k;
}
 
$text = array(
   '1. Purpose' => "

<img src=\"http://bisqwit.iki.fi/src/cromfs-boxart.png\" align=\"left\" alt=\"cromfs\" />
Cromfs is a compressed read-only filesystem for Linux.
It uses the LZMA compression algorithm from <a href=\"http://www.7-zip.com/\">7-zip</a>,
and a powerful block merging mechanism, that is especially efficient
with gigabytes of large files having lots of redundancy.
 <p/>
The primary design goal of cromfs is compression power.
It is much slower than its peers, and uses more RAM.
If all you care about is \"powerful compression\"
and \"random file access\", then you will be happy with cromfs.
 <p/>
The creation of cromfs was inspired
from <a href=\"http://squashfs.sourceforge.net/\">Squashfs</a>
and <a href=\"http://sourceforge.net/projects/cramfs/\">Cramfs</a>.
 <p>
The <a href=\"#download\">downloading</a> section is at the bottom
of this page.

", 'news:1. News' => "

See the <a href=\"http://bisqwit.iki.fi/src/cromfs-changelog.txt\">ChangeLog</a>.

", 'overview:1. Overview' => "

<img src=\"http://bisqwit.iki.fi/src/cromfs-sizedemo.png\" align=\"right\" alt=\"[cromfs size demo]\" />

<ul>
 <li>Data, inodes, directories and block lists are stored compressed</li>
 <li>Files are divided into fragments and those fragments are stored as
  offsets to solid blocks (fblocks) containing data, meaning that parts
  of different files are compressed together for effective compression,
  and identical fragments are compressed only once.
  <ul><li>Duplicate inodes, files and even duplicate file portions are detected
   and stored only once without extra overhead</li></ul>
  </li>
 <li>Most of inode types recognized by Linux are supported (see <a href=\"#compare\">comparisons</a>).</li>
 <li>The <a href=\"http://www.7-zip.com/sdk.html\">LZMA compression</a> is used
  for fblocks. In the general case, LZMA compresses better than gzip and bzip2.</li>
 <li>Being a filesystem, the files on a cromfs volume can be
  randomly accessed in arbitrary order; by all the means one
  would expect, including memorymapping.</li>
 <li>Works on 64-bit and 32-bit systems.</li>
</ul>

See <a href=\"http://bisqwit.iki.fi/src/cromfs-format.txt\"
>the documentation of the cromfs format</a> for technical details
(also included in the source package as doc/FORMAT).

", 'limits:1. Limitations' => "

<ul>
 <li>Filesystem is write-once, read-only. It is not possible to append
  to a previously-created filesystem, nor it is to mount it read-write.</li>
 <li>Max filesize: 2<sup>64</sup> bytes (16777216 TB), but 256 TB with default settings.</li>
 <li>Max number of files in a directory: 2<sup>30</sup> (smaller if filenames are longer, but still more than 100000 in almost all cases)</li>
 <li>Max number of inodes (all files, dirs etc combined): 2<sup>60</sup>, but depends on file sizes</li>
 <li>Max filesystem size: 2<sup>64</sup> bytes (16777216 TB)</li>
 <li>There are no \".\" or \"..\" entries in directories. This does not matter in Linux.</li>
 <li>cromfs and mkcromfs are slower than their peers.</li>
 <li>The cromfs-driver consumes a lot of memory. It is not
   suitable for very size-constrained systems.</li>
 <li>Maximum filename length: 4294967295 bytes</li>
 <li>Maximum symlink length: 65535 bytes</li>
 <li>Being an user-space filesystem, it might not be suitable for
   root filesystems of rescue, tiny-Linux and installation disks.
   (Facts needed.)</li>
 <li>For device inodes, hardlink count of 1 is assumed.
   (This has no effect to compression efficiency.)</li>
</ul>

", 'status:1. Development status' => "

Development status: Stable. (Really: progressive.)<br />
(Fully functional release exists, but is updated from time to time.)
 <p />
Cromfs has been in beta stage for over a year, during which time
very little bugs have been reported, and no known bugs remain at
this time.
 <p />
It does not make sense to keep it as \"beta\" indefinitely,
but since there is never going to be a \"final\" version &mdash;
new versions may always be released &mdash; it is now labeled
as \"progressive\".
 <p />
In practice, the author trusts it works as advertised, but as per GPL policy,
there is NO WARRANTY whatsoever. The entire risk to the quality and performance
of the program suite is with you.
 <pre>#include \"GNU gdb/show warranty\"</pre>

", 'compare:1. Comparing to other filesystems' => "

This is all very biased probably, hypothetical,
and by no means a scientific study, but here goes:

<style type=\"text/css\"><!--
.good  { background:#CFC }
.bad   { background:#FCC }
.hmm   { background:#FFC }
--></style>

<p/>Legend: <span class=good>Good</span>,
<span class=bad>Bad</span>,
<span class=hmm>Partial</span>

<table border=1 class=fscom>
 <tr align=left>
  <th>Feature</th>
   <th>Cromfs</th>
   <th>Cramfs</th>
   <th>Squashfs (3.3)</th>
   <th>Cloop</th>
  </tr>
 <tr align=left>
  <th>Compression unit</th>
   <td class=good>adjustable arbitrarily (2 MB default)</td>
   <td class=hmm>adjustable, must be power of 2 (4 kB default)</td>
   <td class=hmm>adjustable, must be power of 2 (1 MB max)</td>
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
   <td class=hmm>16 EB (2<sup>64</sup> bytes) (theoretical; actual limit depends on settings)</td>
   <td class=bad>16 MB (2<sup>24</sup> bytes)</td>
   <td class=good>16 EB (2<sup>64</sup> bytes)<br /> (4 GB before v3.0)</td>
   <td class=good>Depends on slave filesystem</td>
  </tr>
 <tr align=left>
  <th>Maximum filesystem size</th>
   <td class=good>16 EB (2<sup>64</sup> bytes)</td>
   <td class=bad>272 MB</td>
   <td class=good>16 EB (2<sup>64</sup> bytes)<br /> (4 GB before v3.0)</td>
   <td class=good>16 EB (2<sup>64</sup> bytes)</td>
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
   <td class=hmm>Theoretically safe (untested on bigendian)</td>
   <td class=hmm>Safe, but not exchangeable</td>
   <td class=hmm>Safe, but not exchangeable</td>
   <td class=hmm>Depends on slave filesystem</td>
 <tr align=left>
  <th>Linux kernel driver</th>
   <td class=bad>No</td>
   <td class=good>Yes</td>
   <td class=good>Yes</td>
   <td class=good>Yes</td>
 <tr align=left>
  <th>Userspace driver</th>
   <td class=good>Yes (fuse)</td>
   <td class=bad>No</td>
   <td class=hmm>An extraction tool (unsquashfs)</td>
   <td class=good><a href=\"http://fusecloop.sourceforge.net/\">Yes</a> (third-party, using fuse).<br>
     Cloop itself provides an extraction tool (extract_compressed_fs),
     but cannot be used to extract a single file.</td>
 <tr align=left>
  <th>Windows driver</th>
   <td class=bad>No</td>
   <td class=bad>No</td>
   <td class=bad>No</td>
   <td class=bad>No</td>
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
  <th>Mounting as read-write</th>
   <td class=bad>No</td>
   <td class=bad>No</td>
   <td class=bad>No</td>
   <td class=bad>No</td>
 <tr align=left>
  <th>Supported inode types</th>
   <td class=good>all</td>
   <td class=good>all</td>
   <td class=good>all</td>
   <td class=good>Depends on slave filesystem</td>
 <tr align=left>
  <th>Fragmentation<br />(good for compression, bad for access speed)</th>
   <td class=good>Depends on compression settings</th>
   <td class=hmm>None</td>
   <td class=good>File tails only</td>
   <td class=hmm>Depends on slave filesystem</td>
 <tr align=left>
  <th>Holes (aka. sparse files); storage optimization
      of blocks which consist entirely of nul bytes</th>
   <td class=good>Any two identical blocks are merged and stored only once.</th>
   <td class=good>Supported</td>
   <td class=good>Supported</th>
   <td class=good>Depends on slave filesystem</td>
 <tr align=left>
  <th>Padding (partially filled sectors, wastes space)</th>
   <td class=good>No<!-- (unless readwrite support is asked, but even then it only makes a sparse file)--></td>
   <td>Unknown</td>
   <td class=hmm>Mostly not</td>
   <td class=hmm>Depends on slave filesystem, usually yes</td>
 <tr align=left>
  <th>Extended attributes</th>
   <td class=bad>No</td>
   <td>Unknown</td>
   <td>Unknown</td>
   <td>Unknown, may depend on slave filesystem</td>
</table>
 <p>
Note: If you notice that this table contains wrong information,
please contact me telling what it is and I will change it.
 <p/>
Note: cromfs now saves the uid and gid in the filesystem. However,
when the uid is 0 (root), the cromfs-driver returns the uid of the
user who mounted the filesystem, instead of root. Similarly for gid.
This is both for backward compatibility and for security.<br />
If you mount as root, this behavior has no effect.

", 'compression:1.1. Compression tests' =>"

Note: I use the -e and -r options in all of these mkcromfs tests
to avoid unnecessary decompression+recompression steps, in order
to speed up the filesystem generation. This has no effect in
compression ratio.<br />
<br />
In this table, <i>k</i> equals 1024 bytes (2<sup>10</sup>)
and <i>M</i> equals 1048576 bytes (2<sup>20</sup>).
 <p>
Note: Again, these tests have not been peer-verified so it is not
a real scientific study. But I attest that these are the results I got.

<style type=\"text/css\"><!--
.comcom b  { color:#007 }
.comcom tt { display:block; width:100%; color:#050; background:#EEE }
--></style>
<table border=\"1\" style=\"font-size:12px\" class=\"comcom\">
 <tr>
  <th>Item</th>
  <th align=\"left\">10783 NES ROMs (2523 MB)</th>
  <th align=\"left\">Firefox 2.0.0.5 source code (233 MB)<br />
    (MD5sum 5a6ca3e4ac3ebc335d473cd3f682a916)
    </th>
  <th align=\"left\">Damn small Linux liveCD (113 MB)<br />
   (size taken from \"du -c\" output in the uncompressed filesystem)</th>
 </tr>

 <tr align=\"right\"3 valign=\"top\">
  <th>Cromfs</th>
  <td class=good><tt>mkcromfs -s65536 -c16 -a&hellip; -b&hellip; -f&hellip;</tt>
   <br />With 16M fblocks, 2k blocks: <b>198,553,574</b> bytes (v1.4.1)
   <br />With 16M fblocks, 1k blocks, <b>194,813,427</b> bytes (v1.4.1)
   <br />With 16M fblocks, &frac14;k blocks: <b>187,575,926</b> bytes (v1.5.0)
   </td>
  <td class=good><tt>mkcromfs</tt>
   <br />With default options: <b>33,866,164</b> bytes (v1.5.2)
   <br />(Peak memory use (RSS): 97 MB (mostly comprising of memory-mapped files)
   </td>
  <td class=good><tt>mkcromfs -f1048576</tt>
   <br />With 64k blocks (-b65536), <b>39,778,030</b> bytes (v1.2.0)
   <br />With 16k blocks (-b16384), <b>39,718,882</b> bytes (v1.2.0)
   <br />With 1k blocks (-b1024), <b>40,141,729</b> bytes (v1.2.0)
   </td>
 </tr>

 <tr align=\"right\" valign=\"top\">
  <th>Cramfs v1.1</th>
  <td class=bad><tt>mkcramfs -b65536</tt>
   <br />dies prematurely, \"filesystem too big\"</td>
  <td class=bad><tt>mkcramfs</tt>
   <br />with 2M blocks (-b2097152), <b>65,011,712</b> bytes
   <br />with 64k blocks (-b65536), <b>64,618,496</b> bytes
   <br />with 4k blocks (-b4096), <b>77,340,672</b> bytes
   </td>
  <td class=bad><tt>mkcramfs -b65536</tt>
   <br /><b>51,445,760</b> bytes
   </td>
 </tr>

 <tr align=\"right\" valign=\"top\">
  <th>Squashfs v3.2</th>
  <td class=bad><tt>mksquashfs -b65536</tt>
   <br />(using an optimized sort file) <b>1,185,546,240</b> bytes</td>
  <td class=hmm><tt>mksquashfs</tt>
   <br /><b>49,139,712</b> bytes</td>
  <td class=bad><tt>mksquashfs -b65536</tt>
   <br /><b>50,028,544</b> bytes
    </td>
 </tr>

 <tr align=\"right\" valign=\"top\">
  <th>Cloop v2.05~20060829</th>
  <td class=bad><tt>create_compressed_fs</tt>
   <br />(using an iso9660 image created with mkisofs -R)
   <br />using 7zip, 1M blocks (-B1048576 -t2 -L-1): <b>1,136,789,006</b> bytes
   </td>
  <td class=hmm><tt>create_compressed_fs</tt>
   <br />(using an iso9660 image created with mkisofs -RJ)
   <br />using 7zip, 1M blocks (-B1048576 -L-1): <b>46,726,041</b> bytes
    <br />(1 MB is the maximum block size in cloop)
   </td>
  <td class=bad><tt>create_compressed_fs</tt>
   <br />(using an iso9660 image)
   <br />using 7zip, 1M blocks (-B1048576 -L-1): <b>48,328,580</b> bytes
   <br />using zlib, 64k blocks (-B65536 -L9): <b>50,641,093</b> bytes
   </td>
 </tr>

 <tr align=\"right\" valign=\"top\">
  <th>7-zip (p7zip) v4.30<br /> (an archive, not a filesystem)</th>
  <td><tt>7za -mx9 -ma=2 a</tt>
   <br />with 32M blocks (-md=32m): <b>235,037,017</b> bytes
   <br />with 128M blocks (-md=128m): <b>222,523,590</b> bytes
   <br />with 256M blocks (-md=256m): <b>212,533,778</b> bytes
  <td><tt>7za -mx9 -ma=2 -md=256m a</tt>
   <br /><b>29,079,247</b> bytes
   <br />(Peak memory use: 2545 MiB)</td>
  <td><tt>7za -mx9 -ma2 a</tt>
    <br /><b>37,205,238</b> bytes
   </td>
 </tr>
</table>

An explanation why mkcromfs beats 7-zip in the NES ROM packing test:
<blockquote style=\"font-size:92%;color:#222\">
 7-zip packs all the files together as one stream. The maximum dictionary
 size in 32-bit mode is 256 MB.
 (Note: The default for \"maximum compression\" is 32 MB.)
 When 256 MB of data has been packed and more data comes in,
 similarities between the first megabytes of data and the latest data are
 not utilized. For example, <i>Mega Man</i> and <i>Rockman</i> are two
 almost identical versions of the same image, but because there's more
 than 400 MB of files in between of those when they are processed in
 alphabetical order, 7-zip does not see that they are similar, and will
 compress each one separately.<br />
 7-zip's chances could be improved by sorting the files so that it will
 process similar images sequentially. It already attempts to accomplish
 this by sorting the files by filename extension and filename, but it
 is not always the optimal way, as shown here.<br />
<br />
 mkcromfs however keeps track of all blocks it has encoded, and will remember
 similarities no matter how long ago they were added to the archive.
 (<a href=\"/src/cromfs-blockindexing.txt\">Click here</a> to read
 how it does that.)
 This is why it outperforms 7-zip in this case, even
 when it only used 16 MB fblocks.<br />
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
 <p />
cromfs-driver requires an amount of RAM proportional to a few factors.
It can be approximated with this formula:<p />
 <code>
  Max_RAM_usage = FBLOCK_CACHE_MAX_SIZE &times; fblock_size + READDIR_CACHE_MAX_SIZE &times; 60k + 8 &times; num_blocks
 </code><p />
Where
<ul>
 <li>fblock_size is the value of \"--fblock\" used when the filesystem was created</li>
 <li>FBLOCK_CACHE_MAX_SIZE is a constant defined in cromfs.cc (default: 10)</li>
 <li>READDIR_CACHE_MAX_SIZE is a constant defined in cromfs.cc (default: 3)</li>
 <li>60k is an estimate of a large directory size (2000 files with average name length of 10-20 letters)</li>
 <li>num_blocks is the number of block structures in the filesystem
     (maximum size is <code>ceil(total_size_of_files / block_size)</code>,
      but it may be smaller.)
</ul>
For example, for a 500 MB archive with 16&nbsp;kB blocks and 1&nbsp;MB fblocks,
the memory usage would be around 10.2&nbsp;MB.

", 'usage:1. Getting started' => "

<ol>
 <li>Install the development requirements: make, gcc-c++ and fuse
  <ul>
   <li>Remember that for fuse to work, the kernel must also contain the fuse support.
    Do \"modprobe fuse\", and check if you have \"/dev/fuse\" and check if it works.
    <ul>
    <li>If \"/dev/fuse\" does not exist after loading the \"fuse\" module,
       create it manually (as root): <pre># cd /dev<br /># mknod fuse c 10 229</pre></li>
    <li>If an attempt to read from \"/dev/fuse\" (as root) gives \"no such device\",
    it does not work. If it gives \"operation not permitted\", it might work.</li>
     </ul></li>
  </ul></li>
 <li>Configure the source code:
  <pre>\$ ./configure</pre>
  It will automatically determine your software environment
  (mainly, the features supported by your compiler).
 </li>
 <li>Build the programs:
  <pre>\$ make</pre>
   <p>
  This builds the programs \"cromfs-driver\", \"cromfs-driver-static\",
  \"util/mkcromfs\", \"util/cvcromfs\" and \"util/unmkcromfs\".
   </li>
 <li>Create a sample filesystem:
  <pre>\$ util/mkcromfs . sample.cromfs</pre>
   </li>
 <li>Mount the sample filesystem:
  <pre>\$ mkdir sample
\$ ./cromfs-driver sample.cromfs sample</pre>
   </li>
 <li>Observe the sample filesystem:
  <pre>\$ cd sample
\$ du
\$ ls -al</pre>
   </li>
 <li>Unmounting the filesystem:
  <pre>\$ cd ..
\$ fusermount -u sample</pre>
   </li>
</ol>

", 'tips:1. Tips' => "

", '1.1.1. To improve compression' => "

To improve the compression, try these tips:
<ul>
 <li>Do not change --lzmafastbytes. The default value is 273,
     which is the maximum possible.</li>
 <li>Adjust the block size (--bsize) in mkcromfs. If your files
     have a lot identical content, aligned at a certain boundary,
     use that boundary as the block size value. If you are uncertain,
     use a small value (500-5000) rather than a bigger value (20000-400000).
     Too small values will however make inodes large, so keep it sane.
    <br />
     Note: The value does not need to be a power of two.
  </li>
 <li>Adjust the fblock size (--fsize) in mkcromfs. Larger values
     cause almost always better compression. However, large values
     also increase memory consumption when the filesystem is mounted,
     so keep it sane. If uncertain, use the default value (2097152).
    <br />
     Note: The value does not need to be a power of two.
  </li>
 <li>Adjust the --autoindexperiod option (-A). A smaller value will
     increase the chances of mkcromfs finding an identical block
     from something it already processed (if your data has that
     opportunity). Finding that two blocks are identical always
     means better compression.
  </li>
 <li>Sort your files. Files which have similar or partially
     identical content should be processed right after one other.</li>
 <li>Adjust the --bruteforcelimit option (-c). Larger values will require
     mkcromfs to check more fblocks for each block it encodes (making the
     encoding much slower), in the hope it improves compression.<br />
     Basically, --bruteforcelimit is a way to virtually multiply
     the --fsize (thus improving compression) by an integer factor
     without increasing the memory or CPU usage of cromfs-driver.
     Using it is recommended, unless you want mkcromfs to be fast.<br />
     The upper limit on meaningful values for the -c option is the
     number of fblocks on the resulting filesystem.
     <br />
     If uncertain, try something like the value of <code>33554432 / fsize</code>.
     For 2 MB fblocks, that would make -c16.
  </li>
 <li>You can approximate how many blocks your filesystem will
     have by this formula: <code>total_amount_of_unique_data / bsize</code>.
     <ul>
      <li>If the value is less than 65536, use the
     --16bitblocknums (-2) option. It will theoretically save
     (number_of_blocks*2) bytes of uncompressed room by making
     inodes smaller.</li>
      <li>If the value is less than 16777216, use the
     --24bitblocknums (-3) option. It will theoretically save
     (number_of_blocks) bytes of uncompressed room by making
     inodes smaller.</li>
     </ul>
     Due to LZMA compression, the saving in file size might become
     neglible, but it will make cromfs-driver slightly faster,
     and there are no speed penalties.</li>
 <li>Adjust the --lzmabits values. This affects the compression
     phase of mkcromfs (the last phase after blockifying)
  <ul>
   <li>Use \"--lzmabits full\" if you have
     absolutely no regard for compression time &mdash; it will try each
     and every combination of pb, lp and lc and choose the one that results
     in best LZMA compression &mdash; for every compressed item separately.
     It is 225 times slower than the normal way.</li>
   <li>Use \"--lzmabits auto\" if you want mkcromfs to use a heuristic
     algorithm to choose the parameters based on a few experiments.
     It is 27&hellip;200 times slower than the normal way,
     depending on the data. This is enabled by default. Specifying
     \"full\" or giving the values manually overrides it.</li>
  </ul>
</ul>

", '1.1.1. To improve mkcromfs speed' => "

To improve the filesystem generation speed, try these tips:
<ul>
 <li>Use the --decompresslookups option (-e), if you have the
     diskspace to spare.</li>
 <li>Use a large value for the --randomcompressperiod option,
     for example -r100000. This together with -e will significantly
     improve the speed of mkcromfs, on the cost of temporary disk
     space usage. A small value causes mkcromfs to randomly compress
     one of the temporary fblocks more often. It has no effect to
     the compression ratio of the resulting filesystem.
  </li>
 <li><a name=\"tempdir\"></a>Use the TEMP environment variable to control where the temp
     files are written. Example: <tt>TEMP=~/cromfs-temp ./mkcromfs &hellip;</tt></li>
 <li>Specify a low value for --lzmafastbytes in the mkcromfs command
     line. This will cause LZMA to consume less memory and be faster,
     at the cost of compression power. The default value is 273 (maximum).
     The minimum possible value is 5.</li>
 <li>Use larger block size (--bsize). Smaller blocks mean more blocks
     which means more work. Larger blocks are less work.</li>
 <li>Do not use the --bruteforcelimit option (-c). The default value 0
     means that the candidate fblock will be selected straightforwardly.</li>
 <li>If you have a multicore system, add the --threads option.
     Select --threads 2 if you have a dual core system, for example.
     You can also use a larger value than the number of cores, but
     same guidelines apply as with the -j in GNU make. Currently
     this option does not affect compression power, so it is
     recommended to use it.</li>
   <li>Use \"--lzmabits 2,0,3\" (or other values of your choice) to
     make LZMA compression about 27 times faster, with a slight
     cost of compression power. The default option is \"auto\",
     which tests a number of different lzmabits values to end
     up with hopefully optimal compression.</li>
</ul>

", '1.1.1. To control the memory usage' => "

To control the memory usage, use these tips:
<ul>
 <li>Adjust the fblock size (--fsize). The memory used by cromfs-driver
     is directly proportional to the size of your fblocks. It keeps at
     most 10 fblocks decompressed in the RAM at a time. If your fblocks
     are 4 MB in size, it will use 40 MB at max.</li>
 <li>In mkcromfs, adjust the --autoindexperiod option (-A). This will
     not have effect on the memory usage of cromfs-driver, but it will
     control the memory usage of mkcromfs. If you have lots of RAM, you
     should use smaller --autoindexperiod (because it will improve the chances
     of getting better compression results), and use bigger if you have less RAM.</li>
 <li>Find the CACHE_MAX_SIZE settings in cromfs.cc and edit them. This will
     require recompiling the source. (In future, this should be made a command
     line option for cromfs-driver.)</li>
 <li>In mkcromfs, adjust the block size (--bsize). The RAM usage of mkcromfs
     is directly proportional to the number of blocks (and the filesystem size),
     so smaller blocks require more memory and larger require less.
 <li>Adjust the --blockindexmethod option. Different values of this option
     have different effect on the virtual memory use of mkcromfs (it does
     not affect cromfs-driver, though).
     Use \"--blockindexmethod none\" and \"-A0\" if you want the smallest possible
     memory usage for your selected block size. It has an impact on the compression
     power, but you can compensate it by using a large value for the --bruteforcelimit
     option instead, if you don't mind longer runtime.
</ul>

", '1.1.1. To control the filesystem speed' => "

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

", '1.1.1. Using cromfs with automount' => "

Since version 1.3.0, you can use cromfs in conjunction with the
<a href=\"http://tldp.org/HOWTO/Automount-4.html\"
>automount</a> (autofs) feature present in Linux kernel. This allows
you to mount cromfs volumes automatically on demand, and umount
them when they are not used, conserving free memory.
 <p>
This line in your autofs file (such as auto.misc) will do the trick
(assuming the path you want is \"books\", and your volume
is located at \"/home/myself/books.cromfs\"):
 <p>
<pre>books -fstype=fuse,ro,allow_other    :/usr/local/bin/cromfs-driver\\#/home/myself/books.cromfs</pre>

", 'vocabulary:1. Understanding the concepts' => "

Skip over this section if you don't think yourself as technically inclined.<br/>
<br/>
cromfs workings are explained in a nutshell
<a href=\"http://bisqwit.iki.fi/src/cromfs-understandingblocksandfblocks.txt\">here</a>.

", 'concept_inode:1.1.1. Inode' => "

Every object in a filesystem (from user's side) is an \"inode\".
This includes at least symlinks, directories, files, fifos and device entries.
The inode contains the file attributes and its contents, but <em>not</em> its name.
(The name is contained in a directory listing, along with the reference to the inode.)
This is the traditional way in *nix systems.
 <p/>
When a file is \"hardlinked\" into multiple locations in the filesystem,
the inode is not copied. The inode number just is listed in multiple
directories.<br />
A symlink however, is an entirely new inode unrelated to
the file it points to.
 <p/>
The file attributes and the file contents are stored separately.
In cromfs, the inode contains an array of <a href=\"#concept_blocknumber\"
>block numbers</a>, which are necessary in finding the actual contents of the file.

", 'concept_block:1.1.1. Block' => "

The contents of every file (denoted by the inode) are divided into \"blocks\".
The size of this block is controlled by the --bsize commandline parameter.
For example, if your file is 10000 bytes in size, and your bsize is 4000,
the file contains three blocks: 4000 + 4000 + 2000 bytes.
The inode contains thus three <a href=\"#concept_blocknumber\">block numbers</a>,
which refer to entries in the block table.
 <p/>
Only regular files, symlinks and directories have \"contents\" that need
storing. Device entries for example, do not have associated contents.<br />
The contents of a directory is a list of file names and inode numbers.
 <p/>
Every time mkcromfs stores a new block, a new block number is generated
to denote that particular block (this number is stored in the inode),
and a new <a href=\"#concept_datalocator\">data locator</a> is stored
to describe where the block is found (the locator is stored in the block table).
 <p/>
If mkcromfs reused a previously generated data locator,
only the block number needs to be stored.

", 'concept_fblock:1.1.1. Fblock' => "

Fblock is a storage unit in a cromfs filesystem.
It is the physical container of block data for multiple files.<br />
When mkcromfs creates a new filesystem, it splits each file into blocks
(see above), and for each of those blocks, it determines which fblock
they go to. The maximum fblock size is mandated by the --fsize commandline
parameter.
 <p/>
Each fblock is compressed separately, so a few big fblocks compresses better
than many small fblocks.
Cromfs automatically creates as many fblocks as is needed to store the
contents of the entire filesystem being created.
 <p/>
A fblock is merely a storage.
Regardless of the sizes of the blocks and fblocks, the fblock may
contain any number of blocks, from 1 to upwards (no upper limit).
It is beneficial for blocks to overlap, and this is an important
source of the power of cromfs.
 <p/>
The working principle behind fblocks is: What is the shortest
string that can contain all these substrings?

", 'concept_blocknumber:1.1.1. Block number and block table' => "

The filesystem contains a structure called \"blktab\" (block table),
which is a list of <a href=\"#concept_datalocator\">data locators</a>.
This list is indexed by a block number.<br />
Each locator describes, where to find the particular
<a href=\"#concept_block\">block</a> denoted by this block number.
 <p/>
At the end of the filesystem creation process, the blktab is compressed
and becomes \"blkdata\" before being written into the filesystem.<br />
(These names are only useful when referencing the
<a href=\"http://bisqwit.iki.fi/src/cromfs-format.txt\">filesystem format
documentation</a>; they are not found in the filesystem itself.

", 'concept_datalocator:1.1.1. Data locator' => "

A data locator tells cromfs, where to find the contents of this particular block.
It is composed of an <a href=\"#concept_fblock\">fblock</a> number and an offset
into that fblock.
These locators are stored in the global
<a href=\"#concept_blocknumber\">block table</a>, as explained above.
 <p/>
Multiple files may be sharing same data locators, and multiple data
locators may be pointing to same, partially overlapping data.

", 'concept_blockindex:1.1.1. Block indexing (mkcromfs only)' => "

When mkcromfs stores blocks, it remembers where it stored them, so that
if it later finds an identical block in another file (or the same file),
it won't need to search fblocks again to find a best placement.<br />
The index is a map of block hashes to data locators and block numbers.
 <p/>
The --autoindexperiod (-A) setting can be used to extend this mechanism, that
in addition to the blocks it has already encoded, it will memorize more
locations in those fblocks &mdash; create \"just in case\" data locators
for future use but not actually save them in the block table, unless
they're utilized later.
This helps compression when the number of fblocks searched (--bruteforcelimit)
is low compared to the number of fblocks generated, at the cost of memory
consumed by mkcromfs, and has also potential to make mkcromfs faster
(but also slower).

", 'concept_random_compress:1.1.1. Random compress period (mkcromfs only)' => "

When mkcromfs runs, it generates a temporary file for each fblock of the
resulting filesystem. If your resulting filesystem is large, those fblocks
will take even more of space, a lot anyway.<br />
To save disk space, mkcromfs compresses those fblocks when they are not
accessed. However, if it needs to access them again (to search the contents
for a match), it will need to decompress them first.
 <p/>
This compressing+decompressing may consume lots of time. It does not help
the size of the resulting filesystem; it only saves some temporary disk space.
 <p/>
If you are not concerned about temporary disk space, you should give
the --randomcompressperiod option a large number (such as 10000) to
prevent it from needlessly decompressing+compressing the fblocks
over and over again. This will improve the speed of mkcromfs.
 <p/>
The --decompresslookups option is related. If you use the
--randomcompressperiod option, you should also enable --decompresslookups.
 <p/>
By the way, the temporary files are written into wherever
your <a href=\"#tempdir\">TEMP environment variable</a> points to.
TMP is also recognized.

", 'concept_faq:1.1.1. Where are the inodes stored then?' => "

All the inodes of the filesystem are also stored in a file, together.
That file is packed like any one other file, split into blocks and
scattered into fblocks. That data locator list of that file, is stored
in a special inode called \"inotab\", but it is not seen in any
directory. The \"inotab\" has its own place in the cromfs file.

", 'bootfs:1. Using cromfs in bootdisks and tiny Linux distributions' => "

Cromfs can be used in bootdisks and tiny Linux distributions only
by starting the cromfs-driver from a ramdisk (initrd), and then
pivot_rooting into the mounted filesystem (but not before the
filesystem has been initialized; there is a delay of a few seconds).
 <p/>
Theoretical requirements to use cromfs in the root filesystem:
<ul>
 <li>Cromfs-driver should probably be statically linked
  (the Makefile automatically builds a static version
   since version 1.2.2).</li>
 <li>An initrd, that contains the cromfs-driver program</li>
 <li>Fuse driver in the kernel (it may be loaded from the initrd).</li>
 <li>Constructing an <code>unionfs</code> mount from a ramdisk
     and the cromfs mountpoint to form a writable root</li>
 </li>
</ul>

<b>Do not use cromfs in machines that are low on RAM!</b>

", 'otheruse:1. Other applications of cromfs' => "

The compression algorithm in cromfs can be used to determine how similar
some files are to each others.
 <p/>
This is an example output of the following command:
 <pre>$ unmkcromfs --simgraph fs.cromfs '*.qh' &gt; result.xml</pre>
from a sample filesystem:

<pre>&lt;?xml version=\"1.0\" encoding=\"UTF-8\"?>
&lt;simgraph>
 &lt;volume>
  &lt;total_size>64016101&lt;/total_size>
  &lt;num_inodes>7&lt;/num_inodes>
  &lt;num_files>307&lt;/num_files>
 &lt;/volume>
 &lt;inodes>
  &lt;inode id=\"5595\">&lt;file>45/qb5/ir/basewc.qh&lt;/file>&lt;/inode>
  &lt;inode id=\"5775\">&lt;file>45/qb5/ir/edit.qh&lt;/file>&lt;/inode>
  &lt;inode id=\"5990\">&lt;file>45/qb5/ir/help.qh&lt;/file>&lt;/inode>
  &lt;inode id=\"6220\">&lt;file>45/qb5/ir/oemwc.qh&lt;/file>&lt;/inode>
  &lt;inode id=\"6426\">&lt;file>45/qb5/ir/qbasic.qh&lt;/file>&lt;/inode>
  &lt;inode id=\"18833\">&lt;file>c6ers/newcmds/toolib/doc/contents.qh&lt;/file>&lt;/inode>
  &lt;inode id=\"19457\">&lt;file>c6ers/newcmds/toolib/doc/index.qh&lt;/file>&lt;/inode>
 &lt;/inodes>
 &lt;matches>
  &lt;match inode1=\"5595\" inode2=\"5990\">&lt;bytes>396082&lt;/bytes>&lt;ratio>0.5565442944&lt;/ratio>&lt;/match>
  &lt;match inode1=\"5595\" inode2=\"6220\">&lt;bytes>456491&lt;/bytes>&lt;ratio>0.6414264256&lt;/ratio>&lt;/match>
  &lt;match inode1=\"5990\" inode2=\"6220\">&lt;bytes>480031&lt;/bytes>&lt;ratio>0.6732618693&lt;/ratio>&lt;/match>
 &lt;/matches>
&lt;/simgraph></pre>

It reads a cromfs volume generated earlier, and outputs statistics of it.
Such statistics can be useful in refining further compression, or just
finding useful information regarding the redundancy of the data set.
 <p/>
It follows this DTD:
<pre> &lt;!ENTITY % INTEGER \"#PCDATA\">
 &lt;!ENTITY % REAL \"#PCDATA\">
 &lt;!ENTITY % int \"CDATA\">
 &lt;!ELEMENT simgraph (volume, inodes, matches)>
 &lt;!ELEMENT volume (total_size, num_inodes, num_files)>
 &lt;!ELEMENT total_size (%INTEGER;)>
 &lt;!ELEMENT num_inodes (%INTEGER;)>
 &lt;!ELEMENT num_files (%INTEGER;)>
 &lt;!ELEMENT inodes (inode*)>
 &lt;!ELEMENT inode (file+)>
 &lt;!ATTLIST inode id %int; #REQUIRED>
 &lt;!ELEMENT file (#PCDATA)>
 &lt;!ELEMENT matches (match*)>
 &lt;!ELEMENT match (bytes, ratio)>
 &lt;!ATTLIST match inode1 %int; #REQUIRED>
 &lt;!ATTLIST match inode2 %int; #REQUIRED>
 &lt;!ELEMENT bytes (%INTEGER;)>
 &lt;!ELEMENT ratio (%REAL;)></pre>

Once you have generated the file system, running the <tt>--simgraph</tt> query is
relatively a cheap operation (but still O(n<sup>2</sup>) for the number of files);
it involves analyzing the structures created by mkcromfs, and does not
require any search on the actual file contents. However, it can only report as
fine-grained similarity information as were the options in the generation of
the filesystem (level of compression).

", 'copying:1. Copying and contributing' => "

cromfs has been written by Joel Yliluoma, a.k.a.
<a href=\"http://iki.fi/bisqwit/\">Bisqwit</a>,<br />
and is distributed under the terms of the
<a href=\"http://www.gnu.org/licenses/gpl-3.0.html\">General Public License</a>
version 3 (GPL3).
 <br/>
The LZMA code from the LZMA SDK is in public domain.
 <br/>
The LZO code from liblzo2.03 embedded within is licensed
under GPL version 2 or later.
 <p/>
Patches and other related material can be submitted to the
author
".GetEmail('by e-mail at:', 'Joel Yliluoma', 'bisqwi'. 't@iki.fi')."
 <p/>
The author also wishes to hear if you use cromfs, and for what you
use it and what you think of it.
 <p/>
You can discuss CROMFS at <a href=\"http://freenode.net/\">Freenode</a>,
on <a href=\"irc://freenode.net/cromfs\">#cromfs</a>.

", 'wishlist:1.1. Contribution wishes' => "

The author wishes for the following things to be done
to this package.
<ul>
 <li>Topic: Mature enough to be included in distributions.
  <ul>
   <li>Manual pages of each utility (hopefully somehow autogenerated
    so that they won't be useless when new options are added)</li>
   <li>Improve the configure script to make it cope better
    with different Fuse API versions
    and different compiler versions</li>
   <li>Install and uninstall rules in Makefile</li>
  </ul></li>
 <li>Topic: Increasing useability
  <ul>
   <li>A proof of concept example of utilizing cromfs
    in a root filesystem (with initramfs)</li>
   <li>Add appending support (theoretically doable, just not very fast)</li>
   <li>Add threading in cromfs-driver.
       Needs write-locks in fblock_cache and readdir_cache.
       Possibly in BWT too.
       Also blktab and fblktab if those are being changed.</li>
  </ul></li>
 <li>Topic: Documentation
  <ul>
   <li>Graphical illustration on the filesystem structure
    (fs consists of fblocks, and files are split in blocks
     which are actually indexes to various fblocks)</li>
   <li>Document the modular structure of the source code</li>
 </ul></li>
 <li>Topic: Portability
  <ul>
   <li>Write a Windows filesystem driver? (See
    <a href=\"http://bisqwit.iki.fi/src/cromfs-writingfrontends.txt\"
    >doc/WritingFrontends.txt</a> for instructions.)
     <p>
    I would do it myself, but the
    <a href=\"http://www.microsoft.com/whdc/DevTools/IFSKit/IFSKit_About.mspx\"
    >Microsoft Installable Filesystem Kit</a> is not free. It requires (a lot of) money,
    <a href=\"http://www.microsoft.com/whdc/devtools/ifskit/ServerIFSKitOrderfaq.mspx\"
    >no discounts</a>. Interoperability is not really a thing for Microsoft.<br>
    And, I have no experience on Windows kernel mode programming.
  </li>
   <li>Test on big-endian system</li>
 </ul></li>
 <li>Topic: Increasing compression power
  <ul>
   <li>A fast and powerful approximation of the
   <i>shortest common superstring</i> algorithm
   is needed in mkcromfs.
   <br>
    Input description: A set of strings <i>S<sub>1</sub>, &hellip;, S<sub>n</sub>.</i>
  <br>
    Problem description: What is the shortest string <i>S<sup>'</sup></i>
    such that for
    each <i>S<sub>i</sub>, 1&le;i&le;n</i>, the string <i>S<sub>i</sub></i> appears as a
    substring of <i>S<sup>'</sup></i>?
   <br>
     For example, for input
      [\"digital\",\"organ\",\"tall\",\"ant\"],
      it would produce \"organtdigitall\" or \"digitallorgant\".
   <br>
    Note: This problem seems to reduce into an Asymmetric Travelling
    Salesman Problem, which is NP-hard or NP-complete.
    The task here is to find a good approximation
    that doesn't consume a lot of resources.
  </li></ul>
 </li>
</ul>

", 'requ:1. Requirements' => "

<ul>
 <li>GNU make and gcc-c++ are required to recompile the source code.</li>
 <li>The filesystem works under the <a href=\"http://fuse.sourceforge.net/\">Fuse</a>
  user-space filesystem framework. You need to install both the Fuse kernel
  module and the userspace programs before mounting Cromfs volumes.
  You need Fuse version 2.5.2 or newer.
 </li>
 <li>liblzo2-dev is recommended on i386 platforms.
  If it is missing, mkcromfs will use a version shipped in the package.</li>
</ul>

", 'links:1. Links' => "

<ul>
 <li><a href=\"http://www.nofuture.tv/diary/20070303.html\"
   >Review of cromfs in Days of Speed (in Japanese language)</a>
</li></ul>

");
include '/WWW/progdesc.php';
