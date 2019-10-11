query="*2\r\n\$3\r\nget\r\n\$4\r\nkey1\r\n"
query="*2\r\n\$3\r\nget\r\n\$4\r\nkey2\r\n"
query="*2\r\n\$3\r\nget\r\n\$7\r\nkey2018\r\n"
echo "--------- $query -----------"
#printf "$query" | nc 127.0.0.1 6379
for id in `seq 10 99`; do
  echo $id
  query="*2\r\n\$3\r\nget\r\n\$5\r\nkey$id\r\n"
  #printf "$query" | nc 127.0.0.1 11311 | grep "^\\$\|^*\|^-" 
  printf "$query" | nc 127.0.0.1 11311
done

