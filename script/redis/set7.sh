gunzip -c ./set7.data.gz | nc 127.0.0.1 11311 > set7.tmp

# cat set7.tmp
expected_count=$(gunzip -c set7.data.gz | grep "^*" | wc -l | awk '{print $1}')
count=$(cat set7.tmp | wc -l | awk '{print $1}')
printf "Total lines $count/$expected_count\r\n"

if [ $count -ne $expected_count ]; then
  echo -e "\033[33mFail \033[0m"
  exit 1
else
  echo -e "\033[32mSuccess \033[0m"
fi
echo

