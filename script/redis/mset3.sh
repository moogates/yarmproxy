echo "mset key size is 100000"
cat ./mset3.data | nc 127.0.0.1 11311
echo

for key in key2019; do
  key_len=${#key}
  echo $key $key_len
  query="*2\r\n\$3\r\nget\r\n\$${key_len}\r\n${key}\r\n"
  echo "------ $query --------"
  printf "$query" | nc 127.0.0.1 11311
  echo
done
