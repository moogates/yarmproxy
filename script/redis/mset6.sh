#cat ./mset6.data | nc 127.0.0.1 11311
#echo

for key in key7 key2019 key8; do
  key_len=${#key}
  echo $key $key_len
  query="*2\r\n\$3\r\nget\r\n\$${key_len}\r\n${key}\r\n"
  echo "------ $query --------"
  printf "$query" | nc 127.0.0.1 11311
  #printf "$query" | nc 127.0.0.1 6379
  echo
done

