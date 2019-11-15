#!/bin/bash

YARMPROXY_PORT=11311
if [ $# -gt 0 ]; then
  YARMPROXY_PORT=$1
fi

gunzip -c get4.data.gz | ../yarmnc 127.0.0.1 $YARMPROXY_PORT | grep "^\\$\|^*\|^-"  > get4.tmp
# cat get4.tmp

expected_count=$(gunzip -c get4.data.gz | grep "^*" | wc -l | awk '{print $1}')
count=$(cat get4.tmp | wc -l | awk '{print $1}')

if [ $count -ne $expected_count ]; then
  echo -e "\033[33mFail $count/$expected_count.\033[0m"
  exit 1
else
  echo -e "\033[32mPass $count/$expected_count.\033[0m"
fi

