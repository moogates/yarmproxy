#!/bin/bash

YARMPROXY_PORT=11311
if [ $# -gt 0 ]; then
  YARMPROXY_PORT=$1
fi

query="*5\r\n\$4\r\nmset\r\n\$4\r\nkey1\r\n\$6\r\nvalue1\r\n\$4\r\nkey8\r\n\$6\r\nvalue8\r\n"
query="*9\r\n\$4\r\nmset\r\n\$4\r\nkey1\r\n\$6\r\nvalue1\r\n\$4\r\nkey8\r\n\$6\r\nvalue8\r\n\$4\r\nkey3\r\n\$6\r\nvalue3\r\n\$4\r\nkey7\r\n\$6\r\nvalue7\r\n"
query="*17\r\n\$4\r\nmset\r\n\$4\r\nkey1\r\n\$6\r\nvalue1\r\n\$4\r\nkey8\r\n\$6\r\nvalue8\r\n\$4\r\nkey3\r\n\$6\r\nvalue3\r\n\$4\r\nkey7\r\n\$6\r\nvalue7\r\n\$4\r\nkey1\r\n\$6\r\nvalue1\r\n\$4\r\nkey8\r\n\$6\r\nvalue8\r\n\$4\r\nkey3\r\n\$6\r\nvalue3\r\n\$4\r\nkey7\r\n\$6\r\nvalue7\r\n"
query="*3\r\n\$4\r\nmset\r\n\$4\r\nkey1\r\n\$6\r\nvalue1\r\n"

expected="+OK"
res=$(printf "$query" | ../yarmnc 127.0.0.1 $YARMPROXY_PORT | tr -d '\r\n')

if [ $res != $expected ]; then
  echo -e "\033[33mFail $res.\033[0m"
  exit 1
else
  echo -e "\033[32mPass $res.\033[0m"
fi

