#!/bin/bash

YARMPROXY_PORT=11311
if [ $# -gt 0 ]; then
  YARMPROXY_PORT=$1
fi

gunzip -c ./mset_pipeline_3.data.gz | ../yarmnc 127.0.0.1 $YARMPROXY_PORT > mset_pipeline_3.tmp
#cat mset_pipeline_3.tmp

expected=2
res=$(cat mset_pipeline_3.tmp | wc -l | awk '{print $1}')

if [ $res -ne $expected ]; then
  echo -e "\033[33mFail $res/$expected.\033[0m"
  exit 1
else
  echo -e "\033[32mPass $res/$expected.\033[0m"
fi

