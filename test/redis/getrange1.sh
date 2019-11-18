#!/bin/bash

YARMPROXY_PORT=11311
if [ $# -gt 0 ]; then
  YARMPROXY_PORT=$1
fi


./marshal_set key1 200 | ../yarmnc 127.0.0.1 $YARMPROXY_PORT > /dev/null

query="*4\r\n\$8\r\ngetrange\r\n\$4\r\nkey1\r\n\$2\r\n50\r\n\$2\r\n59\r\n"

expected="\$1000000050__"
res=$(printf "$query" | ../yarmnc 127.0.0.1 11311 | tr -d '\r\n')

if [ $res != $expected ]; then
  printf "\033[33mFail $res.\033[0m\r\n"
  exit 1
else
  printf "\033[32mPass $res.\033[0m\r\n"
fi


