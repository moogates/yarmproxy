query="*2\r\n\$3\r\nset\r\n\$4\r\nkey1\r\n\$6\r\nvalue1\r\n"
printf "$query" | nc 127.0.0.1 11311
