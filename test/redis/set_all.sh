#!/bin/bash

YARMPROXY_PORT=11311
if [ $# -gt 0 ]; then
  YARMPROXY_PORT=$1
fi

for id in `seq 2 9`; do
  echo "./set${id}.sh"
  ./set${id}.sh $YARMPROXY_PORT
  sleep 0.01
done

./set_pipeline_1.sh $YARMPROXY_PORT
./setrange.sh $YARMPROXY_PORT
./setex1.sh $YARMPROXY_PORT
./setex4.sh $YARMPROXY_PORT
./setnx1.sh $YARMPROXY_PORT
./setnx4.sh $YARMPROXY_PORT
