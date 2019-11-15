#!/bin/bash

YARMPROXY_PORT=11311
if [ $# -gt 0 ]; then
  YARMPROXY_PORT=$1
fi

expected=6
res=$(gunzip -c ./mset16.data.gz | ../yarmnc 127.0.0.1 $YARMPROXY_PORT | wc -l | awk '{print $1}')

if [ $res != $expected ]; then
  echo -e "\033[33mFail $res/$expected.\033[0m"
  exit 1
else
  echo -e "\033[32mPass $res/$expected.\033[0m"
fi

