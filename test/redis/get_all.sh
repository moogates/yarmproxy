#!/bin/bash

YARMPROXY_PORT=11311
if [ $# -gt 0 ]; then
  YARMPROXY_PORT=$1
fi

for script in get1.sh get2.sh get3.sh get4.sh ; do
  echo -e "./$script $YARMPROXY_PORT"
  ./$script $YARMPROXY_PORT
  if [ $? -ne 0 ]; then
    echo "test fail on ./$script"
    exit 1
  fi
  echo
done
