query="*2\r\n\$4\r\ndecr\r\n\$8\r\ncounter1\r\n"
printf "$query" | nc 127.0.0.1 11311 
echo
