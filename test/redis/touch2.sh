for id in `seq 1 9`; do
  key=key$id
  ./marshal_set $key 1000 | ../yarmnc 127.0.0.1 11311 > /dev/null
done

query="*10\r\n\$5\r\ntouch\r\n\$4\r\nkey9\r\n\$4\r\nkey8\r\n\$4\r\nkey7\r\n\$4\r\nkey6\r\n\$4\r\nkey5\r\n\$4\r\nkey4\r\n\$4\r\nkey3\r\n\$4\r\nkey2\r\n\$4\r\nkey1\r\n"

expected=":9"
res=$(printf "$query" | ../yarmnc 127.0.0.1 11311 | tr -d '\r\n')

if [ $res == $expected ]; then
  echo -e "\033[32mPass.\033[0m"
  exit 0
else
  echo -e "\033[33mFail.\033[0m"
  exit 1
fi
