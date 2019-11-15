#!/bin/bash

YARMPROXY_PORT=11311
if [ $# -gt 0 ]; then
  YARMPROXY_PORT=$1
fi

count=$(./marshal_mget key 100 | ../yarmnc 127.0.0.1 $YARMPROXY_PORT | grep "^\\$\|^*" | wc -l)

if [ $count -ne 101 ]; then
  echo -e "\033[33mFail $count/101.\033[0m"
  exit 1
else
  echo -e "\033[32mPass $count/101.\033[0m"
fi

