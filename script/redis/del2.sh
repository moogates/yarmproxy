for id in `seq 1 9`; do
  key=key$id
  ./data_gen $key 1000 | nc 127.0.0.1 11311 > /dev/null
done

query="*10\r\n\$3\r\ndel\r\n\$4\r\nkey9\r\n\$4\r\nkey8\r\n\$4\r\nkey7\r\n\$4\r\nkey6\r\n\$4\r\nkey5\r\n\$4\r\nkey4\r\n\$4\r\nkey3\r\n\$4\r\nkey2\r\n\$4\r\nkey1\r\n"
#printf "$query" | nc 127.0.0.1 6379
printf "$query" | nc 127.0.0.1 11311 > del2.tmp

cat del2.tmp

expected=":9"
res=$(cat del2.tmp | tr -d '\r\n')

if [ $res == $expected ]; then
  echo -e "\033[32mPass.\033[0m"
  exit 0
else
  echo -e "\033[33mFail.\033[0m"
  exit 1
fi
