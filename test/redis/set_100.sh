#!/bin/bash

YARMPROXY_PORT=11311
if [ $# -gt 1 ]; then
  YARMPROXY_PORT=$2
fi

size=2000
if [ $# -gt 0 ]; then
  size=$1
fi

for id in `seq 1 100`; do
  key=key$id
  cur_size=$(($size+$id))
  echo $key $cur_size
  ./marshal_set $key $cur_size | ../yarmnc 127.0.0.1 $YARMPROXY_PORT
done
