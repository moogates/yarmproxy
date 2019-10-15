query="*4\r\n\$5\r\nsetex\r\n\$4\r\nk001\r\n\$6\r\n864000\r\n\$6\r\nvalue1\r\n"
printf "$query" | nc 127.0.0.1 11311
exit

query="*4\r\n\$3\r\nset\r\n\$4\r\nkey1\r\n\$6\r\nvalue1\r\n\$2\r\nNX\r\n"
printf "$query" | nc 127.0.0.1 11311

query="*5\r\n\$3\r\nset\r\n\$4\r\nkey1\r\n\$6\r\nvalue1\r\n\$2\r\nEX\r\n\$5\r\n86400\r\n"
printf "$query" | nc 127.0.0.1 11311

query="*6\r\n\$3\r\nset\r\n\$4\r\nkey1\r\n\$6\r\nvalue1\r\n\$2\r\nNX\r\n\$2\r\nEX\r\n\$5\r\n86400\r\n"
printf "$query" | nc 127.0.0.1 11311
