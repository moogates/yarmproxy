#printf "get key1 key2 key3 key4 key5 key6 key7 key8\r\n" | nc 127.0.0.1 11311
#printf "get key1\r\n" | nc 127.0.0.1 11311 | grep "VALUE\|END"
#printf "get key21 key12 key93 key84 key75 key66 key57 key48 key30 key31 key32 key33 key34 key35 key36 key37 key38 key1 key2 key3 key4 key5 key6 key7 key8\r\n" | nc 127.0.0.1 11311 | grep "VALUE\|END"
#printf "get key2 key1\r\n" | nc 127.0.0.1 11311 | grep "VALUE\|END"
printf "get key1 key2 key3 key4 key5 key6 key7 key8\r\n" | nc 127.0.0.1 11311
exit
printf "get key1 key2 key3 key4 key5 key6 key7 key8\r\nget key1 key2 key3 key4 key5 key6 key7 key8\r\nget key1 key2 key3 key4 key5 key6 key7 key8\r\nget key1 key2 key3 key4 key5 key6 key7 key8\r\nget key1 key2 key3 key4 key5 key6 key7 key8\r\n" | nc 127.0.0.1 11311
exit
#echo ""
#printf "get key1\r\n" | nc 127.0.0.1 11311
printf "get key1 key2 key3 key4 key5 key6 key7 key8\r\nget key1 key2 key3 key4 key5 key6 key7 key8\r\nget key1 key2 key3 key4 key5 key6 key7 key8\r\nget key1 key2 key3 key4 key5 key6 key7 key8\r\nget key1 key2 key3 key4 key5 key6 key7 key8\r\nget key1 key2 key3 key4 key5 key6 key7 key8\r\nget key1 key2 key3 key4 key5 key6 key7 key8\r\nget key1 key2 key3 key4 key5 key6 key7 key8\r\nget key1 key2 key3 key4 key5 key6 key7 key8\r\nget key1 key2 key3 key4 key5 key6 key7 key8\r\nget key1 key2 key3 key4 key5 key6 key7 key8\r\nget key1 key2 key3 key4 key5 key6 key7 key8\r\nget key1 key2 key3 key4 key5 key6 key7 key8\r\nget key1 key2 key3 key4 key5 key6 key7 key8\r\nget key1 key2 key3 key4 key5 key6 key7 key8\r\nget key1 key2 key3 key4 key5 key6 key7 key8\r\nget key1 key2 key3 key4 key5 key6 key7 key8\r\nget key1 key2 key3 key4 key5 key6 key7 key8\r\nget key1 key2 key3 key4 key5 key6 key7 key8\r\nget key1 key2 key3 key4 key5 key6 key7 key8\r\n" | nc 127.0.0.1 11311 | grep "VALUE\|END"
echo ""

