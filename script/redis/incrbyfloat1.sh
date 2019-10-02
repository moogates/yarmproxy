query="*3\r\n\$11\r\nincrbyfloat\r\n\$8\r\nmyfloat1\r\n\$4\r\n3.14\r\n"
printf "$query" | nc 127.0.0.1 11311 
#printf "$query" | nc 127.0.0.1 6379
echo

