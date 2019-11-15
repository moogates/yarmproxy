#!/bin/bash

YARMPROXY_PORT=11311
if [ $# -gt 0 ]; then
  YARMPROXY_PORT=$1
fi

gunzip -c ./mget_pipeline_3.data.gz | ../yarmnc 127.0.0.1 $YARMPROXY_PORT | grep "^\\$\|^*" > mget_pipeline_3.tmp

count=$(cat mget_pipeline_3.tmp | wc -l)

if [ $count -ne 1460 ]; then
  echo -e "\033[33mFail $count/1460.\033[0m"
  exit 1
else
  echo -e "\033[32mSuccess $count/1460.\033[0m"
fi

