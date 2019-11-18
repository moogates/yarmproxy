query="*3\r\n\$3\r\nset\r\n\$8\r\nmyfloat1\r\n\$1\r\n0\r\n"
printf "$query" | ../yarmnc 127.0.0.1 $YARMPROXY_PORT > /dev/null

query="*3\r\n\$11\r\nincrbyfloat\r\n\$8\r\nmyfloat1\r\n\$4\r\n3.14\r\n"

expected="\$43.14"
res=$(printf "$query" | ../yarmnc 127.0.0.1 11311 | tr -d '\r\n')

if [ $res != $expected ]; then
  printf "\033[33mFail $res.\033[0m\r\n"
  exit 1
fi

expected="\$46.28"
res=$(printf "$query" | ../yarmnc 127.0.0.1 11311 | tr -d '\r\n')

if [ $res != $expected ]; then
  printf "\033[33mFail $res.\033[0m\r\n"
  exit 2
else
  printf "\033[32mPass $res.\033[0m\r\n"
fi

