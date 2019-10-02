query="*3\r\n\$6\r\ndecrby\r\n\$8\r\ncounter1\r\n\$1\r\n3\r\n"
printf "$query" | nc 127.0.0.1 11311 
echo

