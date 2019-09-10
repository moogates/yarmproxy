cat ./mset5.data | nc 127.0.0.1 11311
echo
exit

#for key in key8 key7 key1 key2; do
for key in key1; do
  key_len=${#key}
  echo $key $key_len
  query="*2\r\n\$3\r\nget\r\n\$${key_len}\r\n${key}\r\n"
  printf "$query" | nc 127.0.0.1 11311
  echo
done

