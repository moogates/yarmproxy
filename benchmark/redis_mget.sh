#!/bin/bash

MARSHAL_MSET=../test/redis/marshal_mset
MARSHAL_MGET=../test/redis/marshal_mget
YARMNC=../test/yarmnc

round=0
$MARSHAL_MGET key 100 > mget_req.tmp

#for body_size in 50 200 500 2000 5000 10000 20000 50000 100000 200000 500000; do
for body_size in 50 100 150 200 300 400 600 900 1300 2000 3000 5000 10000 20000 50000 100000 200000 500000; do
#for body_size in 50 200 500 2000; do
#for body_size in 50; do
  rm -fv redis_mget.$body_size.*.tmp
  color=$((31+round%2))
  echo "Benchmarking redis 'mget' command, body_size $body_size"
  for server in "redis 6379" "yarmproxy-zc 11311" "yarmproxy-nzc 11312" "nutcracker 22121" ;do
  # for server in "yarmproxy 11311" ;do
    name=`echo $server | awk '{print $1}'`
    port=`echo $server | awk '{print $2}'`

    $MARSHAL_MSET key $body_size 100 | $YARMNC 127.0.0.1 $port > /dev/null 2>&1
    printf "\033[${color}m - Running against $name(port=$port) ... \033[0m"
    time for id in `seq 1 200`; do cat mget_req.tmp | $YARMNC 127.0.0.1 $port > /dev/null 2>&1; done
    # time for id in `seq 1 1000`; do cat mget_req.tmp | $YARMNC 127.0.0.1 $port >> redis_mget.$body_size.$name.tmp 2>&1 ; done
  done
  round=$((round+1))
  echo
  echo
done

