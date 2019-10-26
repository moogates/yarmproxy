./marshal_mset key 20 9 | nc 127.0.0.1 11311 > /dev/null

query="*10\r\n\$6\r\nexists\r\n\$4\r\nkey9\r\n\$4\r\nkey8\r\n\$4\r\nkey7\r\n\$4\r\nkey6\r\n\$4\r\nkey5\r\n\$4\r\nkey4\r\n\$4\r\nkey3\r\n\$4\r\nkey2\r\n\$4\r\nkey1\r\n"
#printf "$query" | nc 127.0.0.1 6379
printf "$query" | nc 127.0.0.1 11311 | tee exists2.tmp

expected=":9"
res=$(cat exists2.tmp | tr -d '\r\n')

if [ $res == $expected ]; then
  echo -e "\033[32mPass.\033[0m"
else
  echo -e "\033[33mFail.\033[0m"
  exit 1
fi

./marshal_del key 9 | nc 127.0.0.1 11311 > /dev/null
printf "$query" | nc 127.0.0.1 11311 | tee exists2.tmp

expected=":0"
res=$(cat exists2.tmp | tr -d '\r\n')

if [ $res == $expected ]; then
  echo -e "\033[32mPass.\033[0m"
else
  echo -e "\033[33mFail.\033[0m"
  exit 1
fi
