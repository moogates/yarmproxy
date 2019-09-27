driver=nc
driver=../yarmnc
gunzip -c ./set3.data.gz | $driver 127.0.0.1 11311 > set3.tmp

cat set3.tmp

expected_count=$(gunzip -c set3.data.gz | grep "^*" | wc -l | awk '{print $1}')
count=$(cat set3.tmp | wc -l | awk '{print $1}')
printf "Total lines $count/$expected_count\r\n"

if [ $count -ne $expected_count ]; then
  echo -e "\033[33mFail \033[0m"
  exit 1
else
  echo -e "\033[32mSuccess \033[0m"
  exit 100
fi
echo

