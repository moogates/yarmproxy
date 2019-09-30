query="prepend key1 0 86400 9\r\nvvvvvvvvv\r\n"
# echo "$query"
printf "$query" | nc 127.0.0.1 11311

printf "get key1\r\n" | nc 127.0.0.1 11311
