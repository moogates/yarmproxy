query="*10\r\n\$4\r\nmget\r\n\$4\r\nkeyx\r\n\$4\r\nkey1\r\n\$4\r\nkey1\r\n\$4\r\nkey2\r\n\$4\r\nkey3\r\n\$4\r\nkey1\r\n\$4\r\nkey1\r\n\$4\r\nkey2\r\n\$4\r\nkey3\r\n"
echo "--------- $query -----------"
printf "$query" | nc 127.0.0.1 11311
echo

query="*10\r\n\$4\r\nmget\r\n\$4\r\nkeyx\r\n\$4\r\nkey1\r\n\$4\r\nkey1\r\n\$4\r\nkey2\r\n\$4\r\nkey3\r\n\$4\r\nkey1\r\n\$4\r\nkey1\r\n\$4\r\nkey2\r\n\$4\r\nkey3\r\n"
echo "--------- $query -----------"
printf "$query" | nc 127.0.0.1 11311
echo

