#!/bin/bash

YARMPROXY_PORT=11311
if [ $# -gt 0 ]; then
  YARMPROXY_PORT=$1
fi

for id in `seq 1 4`; do
  echo "./get${id}.sh"
  ./get${id}.sh $YARMPROXY_PORT
  sleep 0.01
done
