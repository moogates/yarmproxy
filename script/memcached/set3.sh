gunzip set3.data.gz -c | nc 127.0.0.1 11311 | tee set3.tmp

count=$(cat set3.tmp | grep -c STORED)
expected=1

if [ $count -ne $expected ]; then
  echo -e "\033[33mFail: set $count/$expected.\033[0m"
  exit 1
else
  echo -e "\033[32mPass: set $count/$expected.\033[0m"
fi

count=$(printf "get key1\r\n" | nc 127.0.0.1 11311 | grep -c "VALUE.*300000")
expected=1

if [ $count -ne $expected ]; then
  echo -e "\033[33mFail: get $count/$expected.\033[0m"
  exit 1
else
  echo -e "\033[32mPass: get $count/$expected.\033[0m"
fi
