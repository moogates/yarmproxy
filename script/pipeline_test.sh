#printf "get key1 key2 key3 key4 key5 key6 key7 key8\r\n" | nc 127.0.0.1 11311
(printf "get key1\r\nget key1\r\nget key1\r\n"; sleep 1) | nc localhost 11211 | grep "VALUE\|END"
echo ""
