query="*2\r\n\$3\r\nget\r\n\$4\r\nkey7\r\n"
query="*2\r\n\$3\r\nget\r\n\$4\r\nkey1\r\n"
echo "--------- $query -----------"
#printf "$query" | nc 127.0.0.1 6379
printf "$query" | nc 127.0.0.1 11311
echo
