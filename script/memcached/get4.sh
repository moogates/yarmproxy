body_size=$(echo "($RANDOM*23+2027)%262144" | bc)
keys_count=50
./set_100.sh $body_size > /dev/null

query="get key1 key2 key3 key4 key5 key6 key7 key8 key9 key10 key11 key12 key13 key14 key15 key16 key17 key18 key19 key20 key21 key22 key23 key24 key25 key26 key27 key28 key29 key30 key31 key32 key33 key34 key35 key36 key37 key38 key39 key40 key41 key42 key43 key44 key45 key46 key47 key48 key49 key50\r\n"

printf "$query" | nc 127.0.0.1 11311 | grep "VALUE\|END" > get4.tmp

#cat get4.tmp
value_lines=$(cat get4.tmp | grep -c $body_size)
if [ $value_lines -ne $keys_count ]; then
  echo -e "\033[33mFail: Response values error.$value_lines.\033[0m"
  exit 1
else
  echo -e "\033[32mResponse values ok, keys_count=$keys_count, body_size=$body_size.\033[0m"
fi

end_line=$(cat get4.tmp | tail -n1 |  tr -d '\r\n')
if [ $end_line != 'END' ]; then
  echo -e "\033[33mFail: End line error.\033[0m"
  exit 1
else
  echo -e "\033[32mEnd line ok.\033[0m"
fi

expected_total_lines=$(echo "$keys_count+1" | bc)
total_lines=$(cat get4.tmp | wc -l |  awk '{print $1}')
if [ $total_lines -ne $expected_total_lines ]; then
  echo -e "\033[33mFail: Total lines error.\033[0m"
  exit 1
else
  echo -e "\033[32mPass.\033[0m"
  exit 0
fi
echo
