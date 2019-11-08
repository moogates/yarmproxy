./marshal_set key1 1000 | nc 127.0.0.1 11311 > /dev/null

query="*2\r\n\$6\r\nexists\r\n\$4\r\nkey1\r\n"
printf "$query" | nc 127.0.0.1 11311
exit
printf "$query" | nc 127.0.0.1 11311 | tee exists1.tmp

expected=":1"
res=$(cat exists1.tmp | tr -d '\r\n')

if [ "$res" == "$expected" ]; then
  echo -e "\033[32mPass.\033[0m"
  exit 0
else
  echo -e "\033[33mFail.\033[0m"
  exit 1
fi

