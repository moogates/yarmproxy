#!/bin/bash

YARMPROXY_PORT=11311
if [ $# -gt 0 ]; then
  YARMPROXY_PORT=$1
fi

printf "Setting up ... "
body_size=$(echo "($RANDOM*23+2027)%262144" | bc)
body_size=100000
echo body_size=$body_size
./set_100.sh $body_size $YARMPROXY_PORT > /dev/null
echo "Done."

for id in `seq 1 4`; do
  echo "./get${id}.sh"
  ./get${id}.sh $YARMPROXY_PORT
  sleep 0.01
done
