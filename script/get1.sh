query="get key1\r\n"
echo "--------- $query -----------"
#printf "$query" | nc 127.0.0.1 11311 | grep "VALUE\|END"
printf "$query" | nc 127.0.0.1 11311
echo $RANDOM
echo
