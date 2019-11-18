./marshal_mset key 20 9 | nc 127.0.0.1 11311 > /dev/null

query="*10\r\n\$6\r\nexists\r\n\$4\r\nkey9\r\n\$4\r\nkey8\r\n\$4\r\nkey7\r\n\$4\r\nkey6\r\n\$4\r\nkey5\r\n\$4\r\nkey4\r\n\$4\r\nkey3\r\n\$4\r\nkey2\r\n\$4\r\nkey1\r\n"

expected=":9"
res=$(printf "$query" | nc 127.0.0.1 11311 | tr -d '\r\n')

if [ $res != $expected ]; then
  printf "\033[33mFail $res.\033[0m\r\n"
  exit 1
fi


./marshal_del key 4 | nc 127.0.0.1 11311 > /dev/null

expected=":5"
res=$(printf "$query" | nc 127.0.0.1 11311 | tr -d '\r\n')

if [ $res != $expected ]; then
  printf "\033[33mFail $res.\033[0m\r\n"
  exit 2
fi

./marshal_del key 9 | nc 127.0.0.1 11311 > /dev/null
expected=":0"
res=$(printf "$query" | nc 127.0.0.1 11311 | tr -d '\r\n')

if [ $res != $expected ]; then
  printf "\033[33mFail $res.\033[0m\r\n"
  exit 3
fi

printf "\033[32mPass $res.\033[0m\r\n"

