#!/bin/bash

YARMPROXY_PORT=11311
if [ $# -gt 0 ]; then
  YARMPROXY_PORT=$1
fi

query="*4\r\n\$6\r\npsetex\r\n\$4\r\nk001\r\n\$6\r\n864000\r\n\$6\r\nvalue1\r\n"
printf "$query" | ../yarmnc 127.0.0.1 $YARMPROXY_PORT
