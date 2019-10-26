#!/bin/bash

YARMPROXY_PORT=11311
if [ $# -gt 0 ]; then
  YARMPROXY_PORT=$1
fi

query="*4\r\n\$8\r\nsetrange\r\n\$4\r\nkey1\r\n\$2\r\n50\r\n\$29\r\nvalue-set-by-setrange-command\r\n"
printf "$query" | ../yarmnc 127.0.0.1 $YARMPROXY_PORT
query="*4\r\n\$8\r\ngetrange\r\n\$4\r\nkey1\r\n\$2\r\n50\r\n\$2\r\n80\r\n"
printf "$query" | ../yarmnc 127.0.0.1 $YARMPROXY_PORT
