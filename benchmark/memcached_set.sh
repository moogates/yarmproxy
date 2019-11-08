#!/bin/bash

MARSHAL_SET=../test/memcached/marshal_set
YARMNC=../test/yarmnc
round=0

for body_size in 50 200 500 2000 5000 10000 20000 50000 100000 200000 500000; do
#for body_size in 50000 100000 200000 500000; do
#for body_size in 50; do
  body_size=$((10000+$round*40000+$RANDOM%20000))
  echo $body_size

  $MARSHAL_SET key101 $body_size > set_req_${body_size}.tmp
  # $MARSHAL_SET key101 $body_size
  color=$((31+round%2))
  echo "Benchmarking 'memcached set' command, body_size $body_size"
  for server in "memcached 11211" "yarmproxy 11311" "nutcracker 22121" ;do
    name=`echo $server | awk '{print $1}'`
    port=`echo $server | awk '{print $2}'`

    printf "\033[${color}m - Running against $name(port=$port) ... \033[0m"
    time for id in `seq 1 100`; do cat set_req_${body_size}.tmp | $YARMNC 127.0.0.1 $port > /dev/null 2>&1; done
  done
  round=$((round+1))
  echo
  echo
done

