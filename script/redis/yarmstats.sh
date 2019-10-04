query="*1\r\n\$9\r\nyarmstats\r\n"
printf "$query" | nc 127.0.0.1 11311
echo

