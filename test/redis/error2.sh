query="*3\r\n\$3\r\nset\r\n\$8\r\nmyfloat1\r\n\$1\r\n0\r\n"
printf "$query" | ../yarmnc 127.0.0.1 $YARMPROXY_PORT > /dev/null

query="*3\r\n\$11\r\nincrbyfloat\r\n\$8\r\nmyfloat1\r\n\$3\r\n3.14\r\n" # bad len value of bulk '3.14', should be 4, not 3
printf "$query" | nc 127.0.0.1 11311 

