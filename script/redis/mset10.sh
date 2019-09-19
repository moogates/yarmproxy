cat ./mset10.data | nc 127.0.0.1 11311
echo

for key in key7 key2018 key9 key2019 key1; do
  key_len=${#key}
  echo $key $key_len
  query="*2\r\n\$3\r\nget\r\n\$${key_len}\r\n${key}\r\n"
  echo "------ $query --------"
  printf "$query" | nc 127.0.0.1 11311
  echo
done

