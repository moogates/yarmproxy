#!/bin/bash

YARMPROXY_PORT=11311
if [ $# -gt 0 ]; then
  YARMPROXY_PORT=$1
fi

for script in set2.sh set3.sh set4.sh set5.sh set6.sh set7.sh set8.sh set9.sh set_pipeline_1.sh setrange.sh setex1.sh setex4.sh setnx1.sh setnx4.sh ; do
  # echo -e "\033[36m./$script $YARMPROXY_PORT \033[0m"
  echo -e "./$script $YARMPROXY_PORT"
  ./$script $YARMPROXY_PORT
  if [ $? -ne 0 ]; then
    echo "test fail on ./$script"
    exit 1
  fi
  echo
done
