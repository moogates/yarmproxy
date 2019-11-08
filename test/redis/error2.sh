query="*3\r\n\$11\r\nincrbyfloat\r\n\$8\r\nmyfloat1\r\n\$3\r\n3.14\r\n" # 3.14的长度不对, 是4而不是3, 导致core dump
printf "$query" | nc 127.0.0.1 11311 
#printf "$query" | nc 127.0.0.1 6379
echo

