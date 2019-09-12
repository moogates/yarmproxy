query="*3\r\n\$4\r\nmget\r\n\$4\r\nkeyx\r\n\$4\r\nkey1\r\n"
echo "--------- $query -----------"
printf "$query" | nc 127.0.0.1 11311
echo

