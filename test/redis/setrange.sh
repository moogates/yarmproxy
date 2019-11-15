#!/bin/bash

YARMPROXY_PORT=11311
if [ $# -gt 0 ]; then
  YARMPROXY_PORT=$1
fi

query="*4\r\n\$8\r\nsetrange\r\n\$4\r\nkey1\r\n\$2\r\n50\r\n\$29\r\nvalue-set-by-setrange-command\r\n"
printf "$query" | ../yarmnc 127.0.0.1 $YARMPROXY_PORT > /dev/null 2>&1
query="*4\r\n\$8\r\ngetrange\r\n\$4\r\nkey1\r\n\$2\r\n50\r\n\$2\r\n57\r\n"
res=$(printf "$query" | ../yarmnc 127.0.0.1 $YARMPROXY_PORT | tr -d '\r\n')

if [ $res != "\$8value-se" ]; then
  echo -e "\033[33mFail.\033[0m"
  exit 1
else
  echo -e "\033[32mPass.\033[0m"
fi

