query="get key1 key2 key3 key4 key5 key6 key7 key88\r\nget key1 key2 key3 key4 key5 key6 key7 key98\r\nget key1 key2 key3 key4 key5 key6 key7 key97\r\nget key1 key2 key3 key4 key5 key6 key7 key92\r\nget key1 key2 key3 key4 key5 key6 key7 key58\r\nget key1 key2 key3 key4 key5 key6 key7 key48\r\nget key1 key2 key3 key4 key5 key6 key7 key68\r\nget key1 key2 key3 key4 key5 key6 key7 key78\r\nget key1 key2 key3 key4 key5 key6 key7 key77\r\nget key1 key2 key3 key4 key5 key6 key7 key66\r\nget key1 key2 key3 key4 key5 key6 key7 key49\r\nget key1 key2 key3 key4 key5 key6 key7 key61\r\nget key1 key2 key3 key4 key5 key6 key7 key31\r\nget key1 key2 key3 key4 key5 key6 key7 key25\r\nget key1 key2 key3 key4 key5 key6 key7 key18\r\nget key1 key2 key3 key4 key5 key6 key7 key91\r\nget key1 key2 key3 key4 key5 key6 key7 key108\r\nget key1 key2 key3 key4 key5 key6 key7 key69\r\nget key1 key2 key3 key4 key5 key6 key7 key93\r\nget key1 key2 key3 key4 key5 key6 key7 key128\r\n"
echo "5.--------- $query -----------"
printf "$query" | nc 127.0.0.1 11311 -w3 | grep "VALUE\|END"
#printf "$query" | nc 127.0.0.1 11311 > x
#cat x | grep "VALUE\|END"
#printf "$query" | nc 127.0.0.1 11211 | grep "VALUE\|END"
echo
