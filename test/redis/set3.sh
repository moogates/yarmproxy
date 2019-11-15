#!/bin/bash

YARMPROXY_PORT=11311
if [ $# -gt 0 ]; then
  YARMPROXY_PORT=$1
fi

gunzip -c ./set3.data.gz | ../yarmnc 127.0.0.1 $YARMPROXY_PORT > set3.tmp

#cat set3.tmp

expected_count=$(gunzip -c set3.data.gz | grep "^*" | wc -l | awk '{print $1}')
count=$(cat set3.tmp | wc -l | awk '{print $1}')

if [ $count -ne $expected_count ]; then
  echo -e "\033[33mFail $count/$expected_count.\033[0m"
  exit 1
else
  echo -e "\033[32mPass $count/$expected_count.\033[0m"
fi

