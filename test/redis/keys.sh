#!/bin/bash

YARMPROXY_PORT=11311
if [ $# -gt 0 ]; then
  YARMPROXY_PORT=$1
fi

printf "*2\r\n\$4\r\nkeys\r\n\$4\r\nkey*\r\n" | ../yarmnc 127.0.0.1 $YARMPROXY_PORT | grep key | sort -n

