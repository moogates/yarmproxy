#!/bin/bash

YARMPROXY_PORT=$YARMPROXY_PORT
if [ $# -gt 0 ]; then
  YARMPROXY_PORT=$1
fi

query="*2\r\n\$3\r\nget\r\n\$4\r\nkey1\r\n*2\r\n\$3\r\nget\r\n\$4\r\nkey2\r\n"
printf "$query" | ../yarmnc 127.0.0.1 $YARMPROXY_PORT | grep "^\\$\|^*\|^-" > get2.tmp

expected_count=2
count=$(cat get2.tmp | wc -l | awk '{print $1}')

if [ $count -ne $expected_count ]; then
  echo -e "\033[33mFail $count/$expected_count.\033[0m"
  exit 1
else
  echo -e "\033[32mPass $count/$expected_count.\033[0m"
fi

