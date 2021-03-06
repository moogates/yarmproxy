KEY=key7
printf "delete $KEY\r\n" | nc 127.0.0.1 11311

query="set $KEY 0 86400 9\r\n穆昭澎\r\n"
# echo "$query"
printf "$query" | nc 127.0.0.1 11311 | tee set1.tmp

count=$(cat set1.tmp | grep -c "^ERROR\|^STORED")
expected=1

if [ $count -ne $expected ]; then
  echo -e "\033[33mFail: set $count/$expected.\033[0m"
  exit 1
else
  echo -e "\033[32mPass: set $count/$expected.\033[0m"
fi

printf "get $KEY\r\n" | nc 127.0.0.1 11311 | tee set1.tmp
count=$(cat set1.tmp | grep -c "^ERROR\|VALUE.*9")
expected=1

if [ $count -ne $expected ]; then
  echo -e "\033[33mFail: get $count/$expected.\033[0m"
  exit 1
else
  echo -e "\033[32mPass: get $count/$expected.\033[0m"
fi
