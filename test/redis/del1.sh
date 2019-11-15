./marshal_set key1 1000 | ../yarmnc 127.0.0.1 11311 > /dev/null

query="*2\r\n\$3\r\ndel\r\n\$4\r\nkey1\r\n"
res=$(printf "$query" | ../yarmnc 127.0.0.1 11311 | tr -d '\r\n')
expected=":1"

if [ $res != $expected ]; then
  echo -e "\033[33mFail $res.\033[0m"
  exit 1
else
  echo -e "\033[32mPass $res.\033[0m"
fi

