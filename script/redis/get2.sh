query="*2\r\n\$3\r\nget\r\n\$4\r\nkey1\r\n*2\r\n\$3\r\nget\r\n\$4\r\nkey2\r\n"
echo "--------- $query -----------"
#printf "$query" | nc 127.0.0.1 6379
for i in `seq 1 10`; do
  printf "$query" | nc 127.0.0.1 11311
  echo -e "round $i ok.\r\n"
done
echo

