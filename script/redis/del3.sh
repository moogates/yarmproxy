printf "Setup begin ..."
for id in `seq 1 30`; do
  key=key$id
  ./marshal_set $key 2027 | nc 127.0.0.1 11311 > /dev/null
done
echo "Done."
query="*31\r\n\$3\r\ndel\r\n\$5\r\nkey10\r\n\$5\r\nkey11\r\n\$5\r\nkey12\r\n\$5\r\nkey13\r\n\$5\r\nkey14\r\n\$5\r\nkey15\r\n\$5\r\nkey16\r\n\$5\r\nkey17\r\n\$5\r\nkey18\r\n\$5\r\nkey19\r\n\$5\r\nkey20\r\n\$5\r\nkey21\r\n\$5\r\nkey22\r\n\$5\r\nkey23\r\n\$5\r\nkey24\r\n\$5\r\nkey25\r\n\$5\r\nkey26\r\n\$5\r\nkey27\r\n\$5\r\nkey28\r\n\$5\r\nkey29\r\n\$5\r\nkey30\r\n\$4\r\nkey9\r\n\$4\r\nkey8\r\n\$4\r\nkey7\r\n\$4\r\nkey6\r\n\$4\r\nkey5\r\n\$4\r\nkey4\r\n\$4\r\nkey3\r\n\$4\r\nkey2\r\n\$4\r\nkey1\r\n"
#printf "$query" | nc 127.0.0.1 6379
printf "$query" | nc 127.0.0.1 11311 > del3.tmp

cat del3.tmp

expected=":30"
res=$(cat del3.tmp | tr -d '\r\n')

if [ $res == $expected ]; then
  echo -e "\033[32mPass.\033[0m"
  exit 0
else
  echo -e "\033[33mFail.\033[0m"
  exit 1
fi
