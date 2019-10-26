body_size=$(echo "($RANDOM*23+2027)%262144" | bc)
./marshal_set key1 $body_size | nc 127.0.0.1 11311 > /dev/null

query="get key1\r\n"
printf "$query" | nc 127.0.0.1 11311 | grep "^ERROR\|^VALUE\|^END" > get1.tmp
cat get1.tmp
value_lines=$(cat get1.tmp | grep -c "^ERROR\|$body_size")
if [ "$value_lines" -ne 1 ]; then
  echo -e "\033[33mFail: Response values error.$value_lines.\033[0m"
  exit 1
else
  echo -e "\033[32mResponse values ok.\033[0m"
fi

end_line=$(cat get1.tmp | tail -n1 | grep -c "^ERROR\|^END")
if [ "$end_line" -ne 1 ]; then
  echo -e "\033[33mFail: End line error.\033[0m"
  exit 1
else
  echo -e "\033[32mEnd line ok.\033[0m"
fi

error_line=$(cat get1.tmp | grep -c "^ERROR")

total_lines=$(cat get1.tmp | wc -l |  awk '{print $1}')
if [ $total_lines -ne 2 -a $error_line -eq 0 ]; then
  echo -e "\033[33mFail: Total lines error.\033[0m"
  exit 1
else
  echo -e "\033[32mPass.\033[0m"
  exit 0
fi
echo
