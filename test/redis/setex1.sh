#!/bin/bash

YARMPROXY_PORT=11311
if [ $# -gt 0 ]; then
  YARMPROXY_PORT=$1
fi

query="*4\r\n\$5\r\nsetex\r\n\$4\r\nk001\r\n\$6\r\n864000\r\n\$6\r\nvalue1\r\n"
res=$(printf "$query" | ../yarmnc 127.0.0.1 $YARMPROXY_PORT | tr -d '\r\n')
if [ $res != '+OK' ]; then
  echo -e "\033[33mFail 1.\033[0m"
  exit 1
fi
echo -e "\033[32mPass \033[0m"

