./marshal_del key 1 | ../yarmnc 127.0.0.1 11311 > /dev/null
query="*2\r\n\$5\r\ntouch\r\n\$4\r\nkey1\r\n"
expected=":0"
res=$(printf "$query" | ../yarmnc 127.0.0.1 11311 | tr -d '\r\n')

if [ $res != $expected ]; then
  echo -e "\033[33mFail.\033[0m"
  exit 1
fi


./marshal_set key1 1000 | ../yarmnc 127.0.0.1 11311 > /dev/null

query="*2\r\n\$5\r\ntouch\r\n\$4\r\nkey1\r\n"
expected=":1"
res=$(printf "$query" | ../yarmnc 127.0.0.1 11311 | tr -d '\r\n')

if [ $res != $expected ]; then
  echo -e "\033[33mFail.\033[0m"
  exit 1
fi

echo -e "\033[32mPass.\033[0m"
