query="add key1 0 86400 9\r\n穆昭澎\r\n"
# echo "$query"
printf "$query" | nc 127.0.0.1 11311

query="replace key1 0 86400 9\r\n穆昭澎\r\n"
# echo "$query"
printf "$query" | nc 127.0.0.1 11311

printf "get key1\r\n" | nc 127.0.0.1 11311
