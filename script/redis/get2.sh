query="*2\r\n\$3\r\nget\r\n\$4\r\nkey1\r\n*2\r\n\$3\r\nget\r\n\$4\r\nkey2\r\n"
# echo "--------- $query -----------"
printf "$query" | nc 127.0.0.1 11311 | grep "^\\$\|^*\|^-" > get2.tmp

expected_count=2
count=$(cat get2.tmp | wc -l | awk '{print $1}')
printf "Total lines $count/$expected_count\r\n"

if [ $count -ne $expected_count ]; then
  echo -e "\033[33mFail \033[0m"
  exit 1
else
  echo -e "\033[32mSuccess \033[0m"
  exit 100
fi
echo

