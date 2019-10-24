#!/bin/bash

YARMPROXY_PORT=11311
if [ $# -gt 0 ]; then
  YARMPROXY_PORT=$1
fi

gunzip -c ./set7.data.gz | ../yarmnc 127.0.0.1 $YARMPROXY_PORT > set7.tmp

# cat set7.tmp
expected_count=$(gunzip -c set7.data.gz | grep "^*" | wc -l | awk '{print $1}')
count=$(cat set7.tmp | wc -l | awk '{print $1}')
printf "Total lines $count/$expected_count\r\n"

if [ $count -ne $expected_count ]; then
  echo -e "\033[33mFail \033[0m"
  exit 1
else
  echo -e "\033[32mSuccess \033[0m"
fi
echo

