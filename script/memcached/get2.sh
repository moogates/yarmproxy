body_size=$(echo "($RANDOM*23+2027)%262144" | bc)
./data_gen $body_size | sed "1s/EXAMPLE_KEY/key1/g" | nc 127.0.0.1 11311 > /dev/null
./data_gen $body_size | sed "1s/EXAMPLE_KEY/key2/g" | nc 127.0.0.1 11311 > /dev/null

query="get key2 key1\r\n"
printf "$query" | nc 127.0.0.1 11311 | grep "VALUE\|END" > get2.tmp

cat get2.tmp
value_lines=$(cat get2.tmp | grep -c $body_size)
if [ $value_lines -ne 2 ]; then
  echo -e "\033[33mFail: Response values error.$value_lines.\033[0m"
  exit 1
else
  echo -e "\033[32mResponse values ok.\033[0m"
fi

end_line=$(cat get2.tmp | tail -n1 |  tr -d '\r\n')
if [ $end_line != 'END' ]; then
  echo -e "\033[33mFail: End line error.\033[0m"
  exit 1
else
  echo -e "\033[32mEnd line ok.\033[0m"
fi

total_lines=$(cat get2.tmp | wc -l |  awk '{print $1}')
if [ $total_lines -ne 3 ]; then
  echo -e "\033[33mFail: Total lines error.\033[0m"
  exit 1
else
  echo -e "\033[32mPass.\033[0m"
  exit 0
fi
echo
