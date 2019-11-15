#!/bin/bash

YARMPROXY_PORT=11311
if [ $# -gt 0 ]; then
  YARMPROXY_PORT=$1
fi

printf "Setting up ... "
base_body_size=100000
base_body_size=$(echo "($RANDOM*23+2027)%262144" | bc)
for id in `seq 10 99`; do
  key=key$id
  cur_size=$((base_body_size+id))
  # echo $key $cur_size
  ./marshal_set $key $cur_size | ../yarmnc 127.0.0.1 $YARMPROXY_PORT > /dev/null
done
echo "Done."

for id in `seq 10 99`; do
  query="*2\r\n\$3\r\nget\r\n\$5\r\nkey$id\r\n"
  reply_size=$(printf "$query" | ../yarmnc 127.0.0.1 $YARMPROXY_PORT | grep "^\\$" | tr -d '\$\r\n')

  expected_size=$((base_body_size+id))
  if [ $reply_size -ne $expected_size ]; then
    echo -e "\033[33mFail $reply_size/$expected_size.\033[0m"
    exit 1
  fi
done
echo -e "\033[32mPass.\033[0m"

