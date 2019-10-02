query="*3\r\n\$6\r\nincrby\r\n\$8\r\ncounter1\r\n\$2\r\n16\r\n"
printf "$query" | nc 127.0.0.1 11311 
echo

