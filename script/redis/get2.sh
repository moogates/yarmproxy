#!/bin/bash

YARMPROXY_PORT=$YARMPROXY_PORT
if [ $# -gt 0 ]; then
  YARMPROXY_PORT=$1
fi

printf "Setting up ... "
body_size=$(echo "($RANDOM*23+2027)%262144" | bc)
body_size=100000
echo body_size=$body_size
./set_100.sh $body_size $YARMPROXY_PORT > /dev/null
echo "Done."

query="*2\r\n\$3\r\nget\r\n\$4\r\nkey1\r\n*2\r\n\$3\r\nget\r\n\$4\r\nkey2\r\n"
# echo "--------- $query -----------"
printf "$query" | ../yarmnc 127.0.0.1 $YARMPROXY_PORT | grep "^\\$\|^*\|^-" > get2.tmp

expected_count=2
count=$(cat get2.tmp | wc -l | awk '{print $1}')
printf "Total lines $count/$expected_count\r\n"

if [ $count -ne $expected_count ]; then
  echo -e "\033[33mFail \033[0m"
  exit 1
else
  echo -e "\033[32mSuccess \033[0m"
  exit 100
fi
echo

