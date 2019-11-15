#!/bin/bash

YARMPROXY_PORT=11311
if [ $# -gt 0 ]; then
  YARMPROXY_PORT=$1
fi

scripts=''
for id in `seq 1 15`; do
  scripts=$(echo "$scripts mset${id}.sh")
done

for id in `seq 1 7`; do
  scripts=$(echo "$scripts mset_pipeline_${id}.sh")
done

for script in $scripts ; do
  echo -e "./$script $YARMPROXY_PORT"
  ./$script $YARMPROXY_PORT
  if [ $? -ne 0 ]; then
    echo "test fail on ./$script"
    exit 1
  fi
  echo
done
