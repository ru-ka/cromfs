#!/bin/bash

sourcedir=/mnt/torrent-downloads/spcsets
# ^ The directory where you have saved the downloaded data from spcsets.torrent,
#   available at http://snesmusic.org/v2/torrent.php

destdir=/mnt/rom-images/spc-src
# ^ The directory where you want to extract the .rsn files and to create
#   the path structure that will be saved in the cromfs image.

## Run this script in order to populate the $destdir with files that
## you will create a cromfs archive of with pack-spc.sh .

fix()
{
  echo "$*"|tr A-Z a-z|tr -d "'"|sed -r 's/[^a-z0-9]+/_/g'
}
fix2()
{
  echo "$*"|sed -r 's/-_+/-/g;s/__+/_/g;s/_+$//'
}
resname()
{
  suff=.spc
  n=1
  while [ -f "$1"$suff ]; do
    #echo "$1"$suff exists... >&2
    if [ "$n" = "1" ]; then
      if [ ! -f "$1"-1$suff ]; then
        mv -v "$1"$suff "$1"-1$suff >&2
        touch "$1"$suff
        for l in *.spc; do
          if [ "`readlink "$l"`" = "$1"$suff ]; then
            ln -sf "$1"-1$suff "$l"
          fi
        done
      fi
    fi
    n=$[n+1]
    suff=-"$n".spc
  done
  echo "$1"$suff
}
is_email()
{
  echo "$1" | egrep -q '^[a-zA-Z0-9.]+@[a-zA-Z0-9.]+$' && echo "1"
}

cd $sourcedir
for s in *.rsn; do
  cd $destdir
  
  short="`echo $s|sed 's/\..*//'`"
  
  mkdir "$short"
  cd "$short"
  unrar x "$sourcedir/$s"
  
  mkdir o
  mv *.spc */ o/
  
  for t in $(find -name \*.spc|sed 's/^..//'); do
    
    prefix="`echo "$t"|sed 's@^o/@./@;s@/[^/]*$@@'`"
    
    prefix="$short-`fix "$prefix"`_"
    
    songtitle="`dd if="$t" bs=1 skip=46 count=32 2>/dev/null|sed 's/ *$//'`"
    gametitle="`dd if="$t" bs=1 skip=78 count=32 2>/dev/null|sed 's/ *$//'`"
    comments="`dd if="$t" bs=1 skip=126 count=32 2>/dev/null|sed 's/ *$//'`"
    
    # Blank fields that look like e-mail addresses
    if [ ! "`is_email "$comments"`" = "" ]; then
      comments=""
    fi
    if [ ! "`is_email "$songtitle"`" = "" ]; then
      songtitle=""
    fi
    
    #model="`echo "$t"|sed 's/^..//'`"
    model="$t"
    
    name="$prefix"_"`fix "$songtitle"`"
    name="`fix2 "$name"`"
    name1="$name"

    if [ ! -z "$songtitle" ]; then
      name="`resname "$name1"`"
      
      if [ -z "$model" ]; then
        mv -v "$t" "$name"
        model="$name"
      else
        ln -vs "$model" "$name"
        model="$name"
      fi
    fi
    
    name="$prefix"_"`fix "$comments"`"
    name="`fix2 "$name"`"
    name2="$name"
    
    if [ ! -z "$comments" -a ! "$name2" = "$name1" ]; then
      name="`resname "$name2"`"
      
      if [ -z "$model" ]; then
        mv -v "$t" "$name"
      else
        ln -vs "$model" "$name"
      fi
    fi
    
    echo "$t: n($name) s($songtitle) g($gametitle) c($comments)"
    echo "--"
  done
  
  find -size 0 -type f | xargs rm
  
  long="`head -n 1 info.txt|tr -d '\015'|tr / _`"
  cd ..
  mv -v "$short" "$long"
  ln -v "$long" -s "$short"
done 2>&1 | tee $destdir/do.log
