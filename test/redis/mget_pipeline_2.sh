#!/bin/bash

YARMPROXY_PORT=11311
if [ $# -gt 0 ]; then
  YARMPROXY_PORT=$1
fi

gunzip -c ./mget_pipeline_2.data.gz | ../yarmnc 127.0.0.1 $YARMPROXY_PORT | grep "^\\$\|^*" > mget_pipeline_2.tmp

count=$(cat mget_pipeline_2.tmp | wc -l)

if [ $count -ne 1500 ]; then
  echo -e "\033[33mFail $count/1500.\033[0m"
  exit 1
else
  echo -e "\033[32mPass $count/1500.\033[0m"
fi

