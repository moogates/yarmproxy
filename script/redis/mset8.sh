#!/bin/bash

YARMPROXY_PORT=11311
if [ $# -gt 0 ]; then
  YARMPROXY_PORT=$1
fi

gunzip -c ./mset8.data.gz | ../yarmnc 127.0.0.1 $YARMPROXY_PORT > mset8.tmp

cat mset8.tmp

expected="+OK"
res=$(cat mset8.tmp | tr -d '\r\n')

if [ $res == $expected ]; then
  echo -e "\033[32mSuccess \033[0m"
  exit 0
else
  echo -e "\033[33mFail \033[0m"
  exit 1
fi

