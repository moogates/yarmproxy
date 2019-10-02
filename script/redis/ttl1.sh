query="*2\r\n\$3\r\nttl\r\n\$4\r\nkey1\r\n"
printf "$query" | nc 127.0.0.1 11311 
echo

