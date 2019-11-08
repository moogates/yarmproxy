#!/bin/bash

YARMPROXY_PORT=11311
if [ $# -gt 0 ]; then
  YARMPROXY_PORT=$1
fi

query="*2\r\n\$3\r\nttl\r\n\$4\r\nkey1\r\n"
printf "$query" | ../yarmnc 127.0.0.1 $YARMPROXY_PORT
echo

