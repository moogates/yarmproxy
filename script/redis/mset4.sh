cat ./mset4.data | nc 127.0.0.1 11311
echo

for key in key1 key2 key8; do
  query="*2\r\n\$3\r\nget\r\n\$4\r\n${key}\r\n"
  printf "$query" | nc 127.0.0.1 11311
  echo
done

