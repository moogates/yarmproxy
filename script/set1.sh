query="set key1 0 86400 6\r\nresult\r\n"
echo "$query"
printf "$query" | nc 127.0.0.1 11311
echo
