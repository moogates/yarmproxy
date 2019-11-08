#!/bin/bash

YARMPROXY_PORT=11311
if [ $# -gt 0 ]; then
  YARMPROXY_PORT=$1
fi

gunzip -c ./mset4.data.gz ./mset5.data.gz ./mset5.data.gz ./mset10.data.gz ./mset6.data.gz ./mset4.data.gz ./mset10.data.gz ./mset7.data.gz ./mset10.data.gz ./mset8.data.gz ./mset10.data.gz ./mset4.data.gz ./mset9.data.gz ./mset13.data.gz ./mset10.data.gz ./mset11.data.gz ./mset12.data.gz ./mset12.data.gz ./mset14.data.gz ./mset15.data.gz > mset_pipeline_7.data.tmp
cat mset_pipeline_7.data.tmp | ../yarmnc 127.0.0.1 $YARMPROXY_PORT > mset_pipeline_7.tmp

expected=$(cat mset_pipeline_7.data.tmp | grep "^*" | wc -l | awk '{print $1}')
res=$(cat mset_pipeline_7.tmp | wc -l | awk '{print $1}')

if [ $res -eq $expected ]; then
  echo -e "\033[32mSuccess $res/$expected.\033[0m"
  exit 0
else
  echo -e "\033[33mFail $res/$expected.\033[0m"
  exit 1
fi

