query="*4\r\n\$8\r\ngetrange\r\n\$4\r\nkey1\r\n\$2\r\n50\r\n\$2\r\n70\r\n"
printf "$query" | nc 127.0.0.1 11311
echo

