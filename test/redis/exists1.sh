./marshal_set key1 1000 | nc 127.0.0.1 11311 > /dev/null

query="*2\r\n\$6\r\nexists\r\n\$4\r\nkey1\r\n"

expected=":1"
res=$(printf "$query" | nc 127.0.0.1 11311 | tr -d '\r\n')

if [ $res != $expected ]; then
  printf "\033[33mFail $res.\033[0m\r\n"
  exit 1
else
  printf "\033[32mPass $res.\033[0m\r\n"
fi

