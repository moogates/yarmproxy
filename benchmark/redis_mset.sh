#!/bin/bash

MARSHAL_MSET=../test/redis/marshal_mset
YARMNC=../test/yarmnc

round=0
#for body_size in 50 200 500 2000 5000 10000 20000 50000 100000 200000 500000; do
for body_size in 20000; do
  $MARSHAL_MSET key $body_size 100 > mset_req_${body_size}.tmp
  color=$((31+round%2))

  rm -frv redis_mset.$body_size.*.bench
  echo "Benchmarking redis 'mset' command, body_size $body_size"
  for server in "redis 6379" "yarmproxy 11311" "nutcracker 22121" ;do
  # for server in "yarmproxy 11311" "nutcracker 22121" ;do
    name=`echo $server | awk '{print $1}'`
    port=`echo $server | awk '{print $2}'`

    printf "\033[${color}m - Running against $name(port=$port) ... \033[0m"
    # time for id in `seq 1 1000`; do cat mset_req_${body_size}.tmp | $YARMNC 127.0.0.1 $port > /dev/null 2>&1; done
    time for id in `seq 1 200`; do cat mset_req_${body_size}.tmp | $YARMNC 127.0.0.1 $port >> redis_mset.$body_size.$name.bench 2>&1; done
  done
  round=$((round+1))
  echo
  echo
done

