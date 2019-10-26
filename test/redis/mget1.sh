#!/bin/bash

YARMPROXY_PORT=11311
if [ $# -gt 0 ]; then
  YARMPROXY_PORT=$1
fi

./marshal_mget key 100 | ../yarmnc 127.0.0.1 $YARMPROXY_PORT | grep "^\\$\|^*"

