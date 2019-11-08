#!/bin/bash

MARSHAL_SET=../test/memcached/marshal_set
MARSHAL_GET=../test/memcached/marshal_get
YARMNC=../test/yarmnc
round=0

for body_size in 50 200 500 2000 5000 10000 20000 50000 100000 200000 500000; do
#for round in `seq 0 100`; do
#for body_size in 50000 100000 200000 500000; do
#for body_size in 50; do
  #body_size=$((1000+$round*4000+$RANDOM%2000))
  #body_size=50
  echo $body_size

  rm -f memcached_set_req.tmp
  for id in `seq 1 10`; do
    $MARSHAL_SET key$id $body_size >> memcached_set_req.tmp
  done
  #cat memcached_set_req.tmp

  $MARSHAL_GET key 10 > memcached_get_req.tmp

  color=$((31+round%5))
  echo "Benchmarking 'memcached get(multi-key)' command, body_size $body_size"
  for server in "memcached 11211" "yarmproxy 11311" "nutcracker 22124" ; do
    printf "\033[${color}m - Running against $name(port=$port) ... \033[0m"
    name=`echo $server | awk '{print $1}'`
    port=`echo $server | awk '{print $2}'`
    cat memcached_set_req.tmp | $YARMNC 127.0.0.1 $port > /dev/null 2>&1

    #time for id in `seq 1 500`; do cat memcached_get_req.tmp | $YARMNC 127.0.0.1 $port > /dev/null 2>&1; done
    time for id in `seq 1 100`; do cat memcached_get_req.tmp | $YARMNC 127.0.0.1 $port 2>&1 | grep "VALUE\|END"; done | tail -n3 
  done
  round=$((round+1))
  echo
  echo
done

