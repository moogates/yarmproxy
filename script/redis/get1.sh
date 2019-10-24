#!/bin/bash

YARMPROXY_PORT=11311
if [ $# -gt 0 ]; then
  YARMPROXY_PORT=$1
fi

printf "Setting up ... "
body_size=$(echo "($RANDOM*23+2027)%262144" | bc)
echo body_size=$body_size
./set_100.sh $body_size $YARMPROXY_PORT > /dev/null
echo "Done."

for id in `seq 10 99`; do
  echo $id
  query="*2\r\n\$3\r\nget\r\n\$5\r\nkey$id\r\n"
  printf "$query" | ../yarmnc 127.0.0.1 $YARMPROXY_PORT | grep "^\\$\|^*\|^-" 
done

