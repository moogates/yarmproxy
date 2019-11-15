#!/bin/bash

YARMPROXY_PORT=11311
if [ $# -gt 0 ]; then
  YARMPROXY_PORT=$1
fi

gunzip -c ./set_pipeline_1.data.gz | ../yarmnc 127.0.0.1 $YARMPROXY_PORT > set_pipeline_1.tmp
#cat set_pipeline_1.tmp

expected=6
res=$(cat set_pipeline_1.tmp | wc -l | awk '{print $1}')

if [ $res -eq $expected ]; then
  echo -e "\033[32mSuccess $res/$expected.\033[0m"
  exit 0
else
  echo -e "\033[33mFail $res/$expected.\033[0m"
  exit 1
fi

