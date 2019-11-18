#!/bin/bash

YARMPROXY_PORT=11311
if [ $# -gt 0 ]; then
  YARMPROXY_PORT=$1
fi

query="*3\r\n\$3\r\nset\r\n\$8\r\ncounter1\r\n\$2\r\n99\r\n"
printf "$query" | ../yarmnc 127.0.0.1 $YARMPROXY_PORT > /dev/null

query="*3\r\n\$6\r\ngetset\r\n\$8\r\ncounter1\r\n\$4\r\n1000\r\n"

expected="\$299"
res=$(printf "$query" | ../yarmnc 127.0.0.1 11311 | tr -d '\r\n')

if [ $res != $expected ]; then
  printf "\033[33mFail $res.\033[0m\r\n"
  exit 1
fi

expected="\$41000"
res=$(printf "$query" | ../yarmnc 127.0.0.1 11311 | tr -d '\r\n')

if [ $res != $expected ]; then
  printf "\033[33mFail $res.\033[0m\r\n"
  exit 2
else
  printf "\033[32mPass $res.\033[0m\r\n"
fi
