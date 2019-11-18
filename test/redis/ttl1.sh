#!/bin/bash

YARMPROXY_PORT=11311
if [ $# -gt 0 ]; then
  YARMPROXY_PORT=$1
fi

./marshal_set key1 200 | ../yarmnc 127.0.0.1 $YARMPROXY_PORT > /dev/null
query="*2\r\n\$3\r\nttl\r\n\$4\r\nkey1\r\n"

expected=":-1"
res=$(printf "$query" | ../yarmnc 127.0.0.1 11311 | tr -d '\r\n')

if [ $res != $expected ]; then
  printf "\033[33mFail $res.\033[0m\r\n"
  exit 1
fi

query="*4\r\n\$5\r\nsetex\r\n\$4\r\nkey1\r\n\$6\r\n864000\r\n\$6\r\nvalue1\r\n"
printf "$query" | ../yarmnc 127.0.0.1 11311 > /dev/null

query="*2\r\n\$3\r\nttl\r\n\$4\r\nkey1\r\n"
expected=":864000"
res=$(printf "$query" | ../yarmnc 127.0.0.1 11311 | tr -d '\r\n')

if [ $res != $expected ]; then
  printf "\033[33mFail 2 $res.\033[0m\r\n"
  exit 2
fi

printf "\033[32mPass $res.\033[0m\r\n"

