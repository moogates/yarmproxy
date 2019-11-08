#!/bin/bash

YARMPROXY_PORT=11311
if [ $# -gt 0 ]; then
  YARMPROXY_PORT=$1
fi

gunzip -c ./mset_pipeline_5.data.gz | ../yarmnc 127.0.0.1 $YARMPROXY_PORT > mset_pipeline_5.tmp

cat mset_pipeline_5.tmp

expected=2
res=$(cat mset_pipeline_5.tmp | wc -l | awk '{print $1}')

if [ $res -eq $expected ]; then
  echo -e "\033[32mSuccess $res/$expected.\033[0m"
  exit 0
else
  echo -e "\033[33mFail $res/$expected.\033[0m"
  exit 1
fi

