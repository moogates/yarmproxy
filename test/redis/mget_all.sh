#!/bin/bash

YARMPROXY_PORT=11311
if [ $# -gt 0 ]; then
  YARMPROXY_PORT=$1
fi

for id in `seq 1 4`; do
  echo -e "\r\n./mget${id}.sh"
  ./mget${id}.sh $YARMPROXY_PORT
done

for id in `seq 1 3`; do
  echo "./mget_pipeline_${id}.sh"
  ./mget_pipeline_${id}.sh $YARMPROXY_PORT
done
