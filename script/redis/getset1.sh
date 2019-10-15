query="*3\r\n\$6\r\ngetset\r\n\$8\r\ncounter1\r\n\$4\r\n1000\r\n"
printf "$query" | nc 127.0.0.1 11311 
echo

