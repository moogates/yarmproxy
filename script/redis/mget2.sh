query="*9\r\n\$4\r\nmget\r\n\$4\r\nkey1\r\n\$4\r\nkey1\r\n\$4\r\nkey2\r\n\$4\r\nkey3\r\n\$4\r\nkey1\r\n\$4\r\nkey1\r\n\$4\r\nkey2\r\n\$4\r\nkey3\r\n"
printf "$query" | nc 127.0.0.1 11311 | grep "^\\$\|^*" > mget2.tmp

count=$(cat mget2.tmp | wc -l)
printf "Total lines $count/9\r\n"

if [ $count -ne 9 ]; then
  echo -e "\033[33mFail \033[0m"
  exit 1
else
  echo -e "\033[32mSuccess \033[0m"
  exit 100
fi
echo

