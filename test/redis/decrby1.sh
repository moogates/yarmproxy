query="*3\r\n\$3\r\nset\r\n\$8\r\ncounter1\r\n\$5\r\n10000\r\n"
printf "$query" | ../yarmnc 127.0.0.1 $YARMPROXY_PORT > /dev/null

query="*3\r\n\$6\r\ndecrby\r\n\$8\r\ncounter1\r\n\$3\r\n100\r\n"

expected=":9900"
res=$(printf "$query" | ../yarmnc 127.0.0.1 11311 | tr -d '\r\n')

if [ $res != $expected ]; then
  printf "\033[33mFail $res.\033[0m\r\n"
  exit 1
else
  printf "\033[32mPass $res.\033[0m\r\n"
fi

