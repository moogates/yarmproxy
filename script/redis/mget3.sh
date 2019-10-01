query="*10\r\n\$4\r\nmget\r\n\$4\r\nk00x\r\n\$4\r\nkey1\r\n\$4\r\nkey1\r\n\$4\r\nkey2\r\n\$4\r\nkey3\r\n\$4\r\nkey1\r\n\$4\r\nkey1\r\n\$4\r\nkey2\r\n\$4\r\nkey3\r\n"
printf "$query" | nc 127.0.0.1 11311 | grep "^\\$\|^*" > mget3.tmp
#printf "$query" | nc 127.0.0.1 6379

count=$(cat mget3.tmp | wc -l)
printf "Total lines $count/10\r\n"

if [ $count -ne 10 ]; then
  echo -e "\033[33mFail \033[0m"
  exit 1
else
  echo -e "\033[32mSuccess \033[0m"
  exit 100
fi
echo

