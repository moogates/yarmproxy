#!/bin/bash

YARMPROXY_PORT=11311
if [ $# -gt 0 ]; then
  YARMPROXY_PORT=$1
fi

gunzip -c ./mset_pipeline_6.data.gz | ../yarmnc 127.0.0.1 $YARMPROXY_PORT > mset_pipeline_6.tmp
#cat mset_pipeline_6.tmp

expected=$(gunzip -c mset_pipeline_6.data.gz |  grep "^*" | wc -l | awk '{print $1}')
count=$(cat mset_pipeline_6.tmp | wc -l | awk '{print $1}')
if [ $count -ne $expected ];
then
  echo -e "\033[33mFail $count/$expected.\033[0m"
  exit 1
else
  echo -e "\033[32mPass $count/$expected.\033[0m"
fi

