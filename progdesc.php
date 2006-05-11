<?php
require_once '/WWW/email.php';

//TITLE=Compressed ROM filesystem

$title = 'Compressed ROM filesystem';
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
<a href=\"http://www.7-zip.com/\">7-zip</a> files, except that practical
realtime access (albeit much slower than on most other filesystems) can be
provided for the whole archive contents; the user does not need to launch
a program to decompress a single file, nor does he need to wait while the
system decompresses 500 files from a 1000-file archive to get him the 1 file
he wanted to open.

 <p/>
The creation of cromfs was inspired
from <a href=\"http://squashfs.sourceforge.net/\">Squashfs</a>
and <a href=\"http://sourceforge.net/projects/cramfs/\">Cramfs</a>.

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
 <li>Ownerships are not saved.</li>
</ul>

Development status: Pre-beta. The Cromfs project has been created
very recently and it hasn't been yet tested extensively. There is no
warranty against data loss or anything else, so use at your own risk.

", 'compare:1. Comparing to other filesystems' => "

This is all very biased, hypothetical, and by no means
a scientific study, but here goes:

<table border=1 class=fscom>
 <tr align=left>
  <th>Feature</th>
   <th>Cromfs</th>
   <th>Cramfs</th>
   <th>Squashfs (3.0)</th>
  </tr>
 <tr align=left>
  <th>Compression unit</th>
   <td>adjustable (1 MB default)</td>
   <td>4 kB</td>
   <td>adjustable (64 kB max)</td>
  </tr>
 <tr align=left>
  <th>Files are compressed</th>
   <td>together (up to block limit)</td>
   <td>individually</td>
   <td>individually</td>
  </tr>
 <tr align=left>
  <th>Maximum file size</th>
   <td>16 EB</td>
   <td>16 MB</td>
   <td>16 EB</td>
  </tr>
 <tr align=left>
  <th>Duplicate whole file detection</th>
   <td>Yes</td>
   <td>No (but hardlinks are detected)</td>
   <td>Yes</td>
  </tr>
 <tr align=left>
  <th>Near-identical file detection</th>
   <td>Yes (identical blocks)</td>
   <td>No</td>
   <td>No</td>
  </tr>
 <tr align=left>
  <th>Compression method</th>
   <td>LZMA</td>
   <td>gzip</td>
   <td>gzip</td>
  </tr>
 <tr align=left>
  <th>Ownerships</th>
   <td>None (uses the mounter's uid and gid)</td>
   <td>uid,gid (but gid truncated to 8 bits)</td>
   <td>uid,gid</td>
 <tr align=left>
  <th>Timestamps</th>
   <td>mtime only</td>
   <td>None</td>
   <td>mtime only</td>
 <tr align=left>
  <th>Endianess-safety</th>
   <td>Works on little-endian only</td>
   <td>Safe, but not exchangeable</td>
   <td>Safe</td>
 <tr align=left>
  <th>Kernelspace/userspace</th>
   <td>User (fuse)</td>
   <td>Kernel</td>
   <td>Kernel</td>
 <tr align=left>
  <th>Appending to a previously created filesystem</th>
   <td>No</td>
   <td>No</td>
   <td>Yes</td>
 <tr align=left>
  <th>Supported inode types</th>
   <td>reg,dir,chrdev,blkdev,fifo,link,sock</td>
   <td>reg,dir,chrdev,blkdev,fifo,link,sock</td>
   <td>reg,dir,chrdev,blkdev,fifo,link,sock</td>
</table>

", 'usage:1. Getting started' => "

<ol>
 <li>Install the development requirements: make, gcc-c++, fuse and openssl-dev
  <ul>
   <li>Remember that for fuse to work, the kernel must also contain the fuse support.
    Do \"modprobe fuse\", and check if you have \"/dev/fuse\" and check if it works.
    <ul><li>If an attempt to read from \"/dev/fuse\" (as root) gives \"no such device\",
    it does not work. If it gives \"operation not permitted\", it might work.</li></ul></li>
  </ul></li>
 <li>Build \"cromfs-driver\" and \"util/mkcromfs\", i.e. command \"make\":
  <pre>\$ make</pre>
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
     use that boundary as the block size value.<br />
     Note: The value does not need to be a power of two.
  </li>
 <li>Adjust the fblock size (--fsize) in mkcromfs. Larger values
     cause almost always better compression. However, the filesystem
     must decompress each fblock as a single unit into the RAM.
     Larger fblock sizes increase the RAM usage of the filesystem.<br />
     Conversely, you can decrease the fblock size in order to save RAM.</li>
 <li>Sort your files. Sometimes the order in which the files
     are compressed has effect on the compressibility.</li>
</ul>

", 'copying:1. Copying' => "

cromfs has been written by Joel Yliluoma, a.k.a.
<a href=\"http://iki.fi/bisqwit/\">Bisqwit</a>,<br>
and is distributed under the terms of the
<a href=\"http://www.gnu.org/licenses/licenses.html#GPL\">General Public License</a> (GPL).
 <p/>
Patches and other related material can be submitted
".GetEmail('by e-mail at:', 'Joel Yliluoma', 'bisqwi'. 't@iki.fi')."

", 'requ:1. Requirements' => "

<ul>
 <li>GNU make and gcc-c++ are required to recompile the source code.</li>
 <li>The openssl development library is required for MD5 calculation.</li>
 <li>The filesystem works under the <a href=\"http://fuse.sourceforge.net/\">Fuse</a>
  user-space filesystem framework. You need to install both the Fuse kernel
  module and the userspace programs before mounting Cromfs volumes.</li>
</ul>

");
include '/WWW/progdesc.php';
