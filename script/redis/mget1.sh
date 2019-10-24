#!/bin/bash

YARMPROXY_PORT=11311
if [ $# -gt 0 ]; then
  YARMPROXY_PORT=$1
fi

query="*3\r\n\$4\r\nmget\r\n\$4\r\nkey1\r\n\$4\r\nkey2\r\n"
query="*3\r\n\$4\r\nmget\r\n\$7\r\nkey2019\r\n\$7\r\nkey2118\r\n"
query="*3\r\n\$4\r\nmget\r\n\$7\r\nkey2019\r\n\$7\r\nkey2018\r\n"
query="*3\r\n\$4\r\nmget\r\n\$4\r\nkey1\r\n\$7\r\nkey2118\r\n"
query="*3\r\n\$4\r\nmget\r\n\$4\r\nkey1\r\n\$4\r\nkey4\r\n"
query="*10\r\n\$4\r\nmget\r\n\$4\r\nkey1\r\n\$4\r\nkey2\r\n\$4\r\nkey3\r\n\$4\r\nkey4\r\n\$4\r\nkey5\r\n\$4\r\nkey6\r\n\$4\r\nkey7\r\n\$4\r\nkey8\r\n\$4\r\nkey9\r\n"
printf "$query" | ../yarmnc 127.0.0.1 $YARMPROXY_PORT

