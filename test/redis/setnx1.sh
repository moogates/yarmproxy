#!/bin/bash

YARMPROXY_PORT=11311
if [ $# -gt 0 ]; then
  YARMPROXY_PORT=$1
fi

query="*2\r\n\$3\r\ndel\r\n\$4\r\nk001\r\n"
printf "$query" | ../yarmnc 127.0.0.1 $YARMPROXY_PORT > /dev/null

query="*3\r\n\$5\r\nsetnx\r\n\$4\r\nk001\r\n\$6\r\nvalue1\r\n"
res=$(printf "$query" | ../yarmnc 127.0.0.1 $YARMPROXY_PORT | tr -d '\r\n')
if [ $res != ':1' ]; then
  echo -e "\033[33mFail 1.\033[0m"
  exit 1
fi

query="*4\r\n\$3\r\nset\r\n\$4\r\nk001\r\n\$6\r\nvalue1\r\n\$2\r\nNX\r\n"
res=$(printf "$query" | ../yarmnc 127.0.0.1 $YARMPROXY_PORT | tr -d '\r\n')
if [ $res != '$-1' ]; then
  echo -e "\033[33mFail 2.\033[0m"
  exit 2
fi

query="*5\r\n\$3\r\nset\r\n\$4\r\nkey1\r\n\$6\r\nvalue1\r\n\$2\r\nEX\r\n\$5\r\n86400\r\n"
res=$(printf "$query" | ../yarmnc 127.0.0.1 $YARMPROXY_PORT | tr -d '\r\n')
if [ $res != '+OK' ]; then
  echo -e "\033[33mFail 3.\033[0m"
  exit 3
fi

query="*6\r\n\$3\r\nset\r\n\$4\r\nkey1\r\n\$6\r\nvalue1\r\n\$2\r\nNX\r\n\$2\r\nEX\r\n\$5\r\n86400\r\n"
res=$(printf "$query" | ../yarmnc 127.0.0.1 $YARMPROXY_PORT | tr -d '\r\n')
if [ $res != '$-1' ]; then
  echo -e "\033[33mFail 4.\033[0m"
  exit 4
fi
echo -e "\033[32mPass \033[0m"

