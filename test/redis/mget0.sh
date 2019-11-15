query="*2\r\n\$4\r\nmget\r\n\$4\r\nkeyx\r\n"
echo "--------- $query -----------"
printf "$query" | ../yarmnc 127.0.0.1 11311
echo

exit

query="*9\r\n\$4\r\nmget\r\n\$4\r\nkey1\r\n\$4\r\nkey1\r\n\$4\r\nkey2\r\n\$4\r\nkey3\r\n\$4\r\nkey1\r\n\$4\r\nkey1\r\n\$4\r\nkey2\r\n\$4\r\nkey3\r\n"
echo "--------- $query -----------"
printf "$query" | ../yarmnc 127.0.0.1 11311
echo

query="*10\r\n\$4\r\nmget\r\n\$4\r\nkeyx\r\n\$4\r\nkey1\r\n\$4\r\nkey1\r\n\$4\r\nkey2\r\n\$4\r\nkey3\r\n\$4\r\nkey1\r\n\$4\r\nkey1\r\n\$4\r\nkey2\r\n\$4\r\nkey3\r\n"
echo "--------- $query -----------"
printf "$query" | ../yarmnc 127.0.0.1 11311
echo

query="*10\r\n\$4\r\nmget\r\n\$4\r\nkeyx\r\n\$4\r\nkey1\r\n\$4\r\nkey1\r\n\$4\r\nkey2\r\n\$4\r\nkey3\r\n\$4\r\nkey1\r\n\$4\r\nkey1\r\n\$4\r\nkey2\r\n\$4\r\nkey3\r\n"
echo "--------- $query -----------"
printf "$query" | ../yarmnc 127.0.0.1 11311
echo
exit

query="get key2 key1\r\n"
echo "1.--------- $query -----------"
#printf "$query" | ../yarmnc 127.0.0.1 11311 | grep "VALUE\|END"
echo

query="get key61 key2 key3 key4 key5 key6 key17 key18 key21 key12 key93 key84 key75 key66 key57 key48 key30 key31 key32 key33 key34 key35 key36 key37 key38\r\n"
echo "--------- $query -----------"
printf "$query" | ../yarmnc 127.0.0.1 11311 > tmp
cat tmp | grep "VALUE\|END"
echo 

exit

query="get key1 key2 key3 key4 key5 key6 key7 key8 key21 key12 key93 key84 key75 key66 key57 key48 key30 key31 key32 key33 key34 key35 key36 key37 key38\r\n"
echo "2.--------- $query -----------"
printf "$query" | ../yarmnc 127.0.0.1 11311 -w 1 > tmp
cat tmp | grep "VALUE\|END"
echo

query="get key1 key2 key3 key4 key5 key6 key7 key8\r\n"
echo "3.--------- $query -----------"
printf "$query" | ../yarmnc 127.0.0.1 11311 > tmp
cat tmp | grep "VALUE\|END"
echo

query="get key1 key2 key3 key4 key5 key6 key7 key8\r\nget key1 key2 key3 key4 key5 key6 key7 key8\r\nget key1 key2 key3 key4 key5 key6 key7 key8\r\nget key1 key2 key3 key4 key5 key6 key7 key8\r\nget key1 key2 key3 key4 key5 key6 key7 key8\r\n"
echo "4.--------- $query -----------"
printf "$query" | ../yarmnc 127.0.0.1 11311 > tmp
cat tmp | grep "VALUE\|END"
echo

query="get key1 key2 key3 key4 key5 key6 key7 key88\r\nget key1 key2 key3 key4 key5 key6 key7 key98\r\nget key1 key2 key3 key4 key5 key6 key7 key97\r\nget key1 key2 key3 key4 key5 key6 key7 key92\r\nget key1 key2 key3 key4 key5 key6 key7 key58\r\nget key1 key2 key3 key4 key5 key6 key7 key48\r\nget key1 key2 key3 key4 key5 key6 key7 key68\r\nget key1 key2 key3 key4 key5 key6 key7 key78\r\nget key1 key2 key3 key4 key5 key6 key7 key77\r\nget key1 key2 key3 key4 key5 key6 key7 key66\r\nget key1 key2 key3 key4 key5 key6 key7 key49\r\nget key1 key2 key3 key4 key5 key6 key7 key61\r\nget key1 key2 key3 key4 key5 key6 key7 key31\r\nget key1 key2 key3 key4 key5 key6 key7 key25\r\nget key1 key2 key3 key4 key5 key6 key7 key18\r\nget key1 key2 key3 key4 key5 key6 key7 key91\r\nget key1 key2 key3 key4 key5 key6 key7 key108\r\nget key1 key2 key3 key4 key5 key6 key7 key69\r\nget key1 key2 key3 key4 key5 key6 key7 key93\r\nget key1 key2 key3 key4 key5 key6 key7 key128\r\n"
echo "5.--------- $query -----------"
printf "$query" | ../yarmnc 127.0.0.1 11311 > tmp
cat tmp | grep "VALUE\|END"
echo
