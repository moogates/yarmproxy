#!/bin/bash

MARSHAL_MSET=../test/redis/marshal_mset
MARSHAL_GET=../test/redis/marshal_get
YARMNC=../test/yarmnc

round=0
$MARSHAL_GET key1 > get_req.tmp

for body_size in 50 200 500 2000 5000 10000 20000 50000 100000 200000 500000; do
  body_size=$((100000+$round*20000+$RANDOM%2000))
  echo $body_size
#for body_size in 50 200 500 2000; do
#for body_size in 50; do
#for body_size in 20000 50000 100000 200000 500000; do
  rm -fv redis_get.$body_size.*.output
  color=$((31+round%2))
  echo "Benchmarking redis 'get' command, body_size $body_size"
  for server in "redis 6379" "yarmproxy 11311" "nutcracker 22121" ;do
  # for server in "yarmproxy 11311" ;do
    name=`echo $server | awk '{print $1}'`
    port=`echo $server | awk '{print $2}'`

    $MARSHAL_MSET key $body_size 1 | $YARMNC 127.0.0.1 $port > /dev/null 2>&1
    printf "\033[${color}m - Running against $name(port=$port) ... \033[0m"
    time for id in `seq 1 600`; do cat get_req.tmp | $YARMNC 127.0.0.1 $port > /dev/null 2>&1; done
    # time for id in `seq 1 200`; do cat get_req.tmp | $YARMNC 127.0.0.1 $port >> redis_get.$body_size.$name.output 2>&1 ; done
  done
  round=$((round+1))
  echo
  echo
done

