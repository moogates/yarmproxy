#!/bin/bash

MARSHAL_SET=../test/memcached/marshal_set
MARSHAL_GET=../test/memcached/marshal_get
YARMNC=../test/yarmnc
color=31

for body_size in 50 200 500 2000 5000 10000 20000 50000 100000 200000 500000; do
#for body_size in 50000 100000 200000 500000; do
#for body_size in 50; do
  #body_size=$((10000+$round*40000+$RANDOM%20000))
  echo $body_size

  $MARSHAL_SET key1 $body_size > memcached_set_req.tmp
  $MARSHAL_GET key 1 > memcached_get_req.tmp
  color=$((31+(color-31)%5))
  echo "Benchmarking 'memcached get' command, body_size $body_size"
  for server in "memcached 11211" "yarmproxy 11311" "nutcracker 22124" ; do
    name=`echo $server | awk '{print $1}'`
    port=`echo $server | awk '{print $2}'`
    cat memcached_set_req.tmp | $YARMNC 127.0.0.1 $port > /dev/null 2>&1

    printf "\033[${color}m - Running against $name(port=$port) ... \033[0m"
    time for id in `seq 1 500`; do cat memcached_get_req.tmp | $YARMNC 127.0.0.1 $port > /dev/null 2>&1; done
    #time for id in `seq 1 10`; do cat memcached_get_req.tmp | $YARMNC 127.0.0.1 $port 2>&1 | grep "VALUE\|END"; done
  done
  color=$((color+1))
  echo
  echo
done

