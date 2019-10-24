#!/bin/bash

YARMPROXY_PORT=11311
if [ $# -gt 0 ]; then
  YARMPROXY_PORT=$1
fi

printf "Setting up ... "
body_size=$(echo "($RANDOM*23+2027)%262144" | bc)
body_size=100000
echo body_size=$body_size
./set_100.sh $body_size $YARMPROXY_PORT > /dev/null
echo "Done."

gunzip -c get3.data.gz | ../yarmnc 127.0.0.1 $YARMPROXY_PORT | grep "^\\$\|^*\|^-"  > get3.tmp

# cat get3.tmp

expected_count=$(gunzip -c get3.data.gz | grep "^*" | wc -l | awk '{print $1}')
count=$(cat get3.tmp | wc -l | awk '{print $1}')
printf "Total lines $count/$expected_count\r\n"

if [ $count -ne $expected_count ]; then
  echo -e "\033[33mFail \033[0m"
  exit 1
else
  echo -e "\033[32mSuccess \033[0m"
  exit 100
fi
echo

