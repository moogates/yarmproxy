body_size=$(echo "($RANDOM*23+2027)%262144" | bc)
keys_count=25
./set_100.sh $body_size > 0

query="get key61 key2 key3 key4 key5 key6 key17 key18 key21 key12 key93 key84 key75 key66 key57 key48 key30 key31 key32 key33 key34 key35 key36 key37 key38\r\n"
#printf "$query" | nc 127.0.0.1 11311 | grep "VALUE\|END"

printf "$query" | nc 127.0.0.1 11311 | grep "VALUE\|END" > get3.tmp

cat get3.tmp
value_lines=$(cat get3.tmp | grep $body_size | wc -l | awk '{print $1}')
if [ $value_lines -ne $keys_count ]; then
  echo -e "\033[33mFail: Response values error.$value_lines.\033[0m"
  exit 1
else
  echo -e "\033[32mResponse values ok.\033[0m"
fi

end_line=$(cat get3.tmp | tail -n1 |  tr -d '\r\n')
if [ $end_line != 'END' ]; then
  echo -e "\033[33mFail: End line error.\033[0m"
  exit 1
else
  echo -e "\033[32mEnd line ok.\033[0m"
fi

expected_total_lines=$(echo "$keys_count+1" | bc)
total_lines=$(cat get3.tmp | wc -l |  awk '{print $1}')
if [ $total_lines -ne $expected_total_lines ]; then
  echo -e "\033[33mFail: Total lines error.\033[0m"
  exit 1
else
  echo -e "\033[32mPass.\033[0m"
  exit 0
fi
echo
