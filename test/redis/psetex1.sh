#!/bin/bash

YARMPROXY_PORT=11311
if [ $# -gt 0 ]; then
  YARMPROXY_PORT=$1
fi

query="*4\r\n\$6\r\npsetex\r\n\$4\r\nk001\r\n\$3\r\n500\r\n\$6\r\nvalue1\r\n"
printf "$query" | ../yarmnc 127.0.0.1 $YARMPROXY_PORT > /dev/null

query="*2\r\n\$3\r\nget\r\n\$4\r\nk001\r\n"
reply_size=$(printf "$query" | ../yarmnc 127.0.0.1 $YARMPROXY_PORT | grep "^\\$" | tr -d '\$\r\n')

expected_size="6"
if [ $reply_size != $expected_size ]; then
  printf "\033[33mFail 1 $reply_size.\033[0m\r\n"
  exit 1
fi

sleep 0.2
reply_size=$(printf "$query" | ../yarmnc 127.0.0.1 $YARMPROXY_PORT | grep "^\\$" | tr -d '\$\r\n')
expected_size="6"
if [ $reply_size != $expected_size ]; then
  printf "\033[33mFail 2 $reply_size.\033[0m\r\n"
  exit 2
fi

sleep 0.7
reply_size=$(printf "$query" | ../yarmnc 127.0.0.1 $YARMPROXY_PORT | grep "^\\$" | tr -d '\$\r\n')
expected_size="-1"
if [ $reply_size != $expected_size ]; then
  printf "\033[33mFail 3 $reply_size.\033[0m\r\n"
  exit 3
fi

printf "\033[32mPass.\033[0m\r\n"
