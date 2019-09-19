query="get key61 key2 key3 key4 key5 key6 key17 key18 key21 key12 key93 key84 key75 key66 key57 key48 key30 key31 key32 key33 key34 key35 key36 key37 key38\r\n"
echo "$query"
#printf "$query" | nc 127.0.0.1 11311 | grep "VALUE\|END"
printf "$query" | nc 127.0.0.1 11311
echo 
