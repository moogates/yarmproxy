driver=nc
driver=../yarmnc

printf "Setting up ... "
body_size=$(echo "($RANDOM*23+2027)%262144" | bc)
body_size=100000
./set_100.sh $body_size > /dev/null
#./set_100.sh $body_size
echo "Done."

#gunzip -c ./mget_pipeline_3.data.gz | $driver 127.0.0.1 11311 | grep "^\\$\|^*"
#gunzip -c ./mget_pipeline_3.data.gz | $driver 127.0.0.1 6379
#exit
gunzip -c ./mget_pipeline_3.data.gz | $driver 127.0.0.1 11311 | grep "^\\$\|^*" > mget_pipeline_3.tmp

count=$(cat mget_pipeline_3.tmp | wc -l)
printf "Total lines $count/1460\r\n"

if [ $count -ne 1460 ]; then
  echo -e "\033[33mFail \033[0m"
  exit 1
else
  echo -e "\033[32mSuccess \033[0m"
fi
echo

