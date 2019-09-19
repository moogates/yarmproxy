cat ./mset4.data | nc 127.0.0.1 11311
echo

for key in key1 key2 key8; do
  query="*2\r\n\$3\r\nget\r\n\$4\r\n${key}\r\n"

  printf "$query" | nc 127.0.0.1 6379 > x
  printf "$query" | nc 127.0.0.1 11311 > y
  diff x y
  if [ $? -ne 0 ]; then
    echo -e "\033[33m fail \033[0m"
  else
    echo -e "\033[32m success \033[0m"
  fi
  echo
done

