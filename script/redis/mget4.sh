query="*73\r\n\$4\r\nmget\r\n\$4\r\nkeyx\r\n\$4\r\nkey1\r\n\$4\r\nkey1\r\n\$4\r\nkey2\r\n\$4\r\nkey3\r\n\$4\r\nkey1\r\n\$4\r\nkey1\r\n\$4\r\nkey2\r\n\$4\r\nkey3\r\n\$4\r\nkeyx\r\n\$4\r\nkey1\r\n\$4\r\nkey1\r\n\$4\r\nkey2\r\n\$4\r\nkey3\r\n\$4\r\nkey1\r\n\$4\r\nkey1\r\n\$4\r\nkey2\r\n\$4\r\nkey3\r\n\$4\r\nkeyx\r\n\$4\r\nkey1\r\n\$4\r\nkey1\r\n\$4\r\nkey2\r\n\$4\r\nkey3\r\n\$4\r\nkey1\r\n\$4\r\nkey1\r\n\$4\r\nkey2\r\n\$4\r\nkey3\r\n\$4\r\nkeyx\r\n\$4\r\nkey1\r\n\$4\r\nkey1\r\n\$4\r\nkey2\r\n\$4\r\nkey3\r\n\$4\r\nkey1\r\n\$4\r\nkey1\r\n\$4\r\nkey2\r\n\$4\r\nkey3\r\n\$4\r\nkeyx\r\n\$4\r\nkey1\r\n\$4\r\nkey1\r\n\$4\r\nkey2\r\n\$4\r\nkey3\r\n\$4\r\nkey1\r\n\$4\r\nkey1\r\n\$4\r\nkey2\r\n\$4\r\nkey3\r\n\$4\r\nkeyx\r\n\$4\r\nkey1\r\n\$4\r\nkey1\r\n\$4\r\nkey2\r\n\$4\r\nkey3\r\n\$4\r\nkey1\r\n\$4\r\nkey1\r\n\$4\r\nkey2\r\n\$4\r\nkey3\r\n\$4\r\nkeyx\r\n\$4\r\nkey1\r\n\$4\r\nkey1\r\n\$4\r\nkey2\r\n\$4\r\nkey3\r\n\$4\r\nkey1\r\n\$4\r\nkey1\r\n\$4\r\nkey2\r\n\$4\r\nkey3\r\n\$4\r\nkeyx\r\n\$4\r\nkey1\r\n\$4\r\nkey1\r\n\$4\r\nkey2\r\n\$4\r\nkey3\r\n\$4\r\nkey1\r\n\$4\r\nkey1\r\n\$4\r\nkey2\r\n\$4\r\nkey3\r\n"

driver=nc
driver=../yarmnc
printf "$query" | $driver 127.0.0.1 11311 | grep "^\\$\|^*" > mget4.tmp

count=$(cat mget4.tmp | wc -l)
printf "Total lines $count/73\r\n"

if [ $count -ne 73 ]; then
  echo -e "\033[33mFail \033[0m"
  exit 1
else
  echo -e "\033[32mSuccess \033[0m"
  exit 100
fi
echo

