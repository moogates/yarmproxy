#!/bin/bash

YARMPROXY_PORT=11311
if [ $# -gt 0 ]; then
  YARMPROXY_PORT=$1
fi

query="*3\r\n\$6\r\ngetset\r\n\$8\r\ncounter1\r\n\$4\r\n1000\r\n"
printf "$query" | ../yarmnc 127.0.0.1 $YARMPROXY_PORT
echo

