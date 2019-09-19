query="get key1 key2 key3 key4 key5 key6 key7 key8\r\n"
echo "--------- $query -----------"
printf "$query" | nc 127.0.0.1 11311 | grep "VALUE\|END"
echo

