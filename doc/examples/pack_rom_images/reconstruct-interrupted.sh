cromfsfile="snes-1024e.cromfs"
stockbase="/mnt/cromtmp/fblock_10976-"

C="$cromfsfile"
S="$stockbase"

F="`dd if="$C" bs=4 skip=4 count=1 | hexdump -e '"%u\n"'`"

n=0
while : ; do
  I="$S""$n"
  
  echo "fblock $n ..."

  size=""
  size="`dd if="$C" bs=1 skip=$F count=4 | hexdump -e '"%u\n"'`"
  
  echo "fblock $n: got $size"
  
  if [ "$size" -gt 0 ]; then # -a ! -f "$I" ]; then
    echo "- skipping"
    F=$[F+4+size]
    #dd if="$C" bs=1 seek="$[F+4]" count="$size"
  else
    if [ -f "$I" ]; then
      size="`stat -c"%s" "$I"`"
      echo "- writing $size"
      #( perl -e "print pack('V',$size)"; dd if="$I" ) | \
      #  ( echo -n '' | dd bs=1 of="$C" seek="$F"
      #    cat >> "$C"
      #  )
      F=$[F+4+size]
    else
      echo -n '' | dd bs=1 of="$C" seek="$F"
    fi
  fi
  
  n=$[n+1]
done
