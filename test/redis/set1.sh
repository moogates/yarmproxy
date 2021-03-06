#!/bin/bash

YARMPROXY_PORT=11311
if [ $# -gt 0 ]; then
  YARMPROXY_PORT=$1
fi

query="*3\r\n\$3\r\nset\r\n\$4\r\nkey1\r\n\$6\r\nvalue1\r\n"
printf "$query" | ../yarmnc 127.0.0.1 $YARMPROXY_PORT

query="*4\r\n\$3\r\nset\r\n\$4\r\nkey1\r\n\$6\r\nvalue1\r\n\$2\r\nNX\r\n"
printf "$query" | ../yarmnc 127.0.0.1 $YARMPROXY_PORT

query="*5\r\n\$3\r\nset\r\n\$4\r\nkey1\r\n\$6\r\nvalue1\r\n\$2\r\nEX\r\n\$5\r\n86400\r\n"
printf "$query" | ../yarmnc 127.0.0.1 $YARMPROXY_PORT

query="*6\r\n\$3\r\nset\r\n\$4\r\nkey1\r\n\$6\r\nvalue1\r\n\$2\r\nNX\r\n\$2\r\nEX\r\n\$5\r\n86400\r\n"
printf "$query" | ../yarmnc 127.0.0.1 $YARMPROXY_PORT
