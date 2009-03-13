INFILE=neswork/gba-*e.log

echo -n 'Memory use/MB: '
(echo 'ibase=16;';echo '-('`grep 'rw-p.*00:00' /proc/$(pidof mkcromfs)/maps|sed 's/ .*//;s/$/)+/;s/^/(0/;s/-/-0/'`'0)/100000'|tr a-f A-Z)|bc -l

tail  $INFILE -n4
lsof -p $(pidof mkcromfs)|grep -v /lib/
