#!/bin/bash

YARMPROXY_PORT=11311
if [ $# -gt 0 ]; then
  YARMPROXY_PORT=$1
fi

for id in `seq 1 15`; do
  echo "./mset${id}.sh"
  ./mset${id}.sh $YARMPROXY_PORT
done

for id in `seq 1 7`; do
  echo "./mset_pipeline_${id}.sh"
  ./mset_pipeline_${id}.sh $YARMPROXY_PORT
done
