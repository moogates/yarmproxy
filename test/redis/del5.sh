printf "Setup begin ..."
for id in `seq 1 30`; do
  key=key$id
  ./marshal_set $key 2027 | ../yarmnc 127.0.0.1 11311 > /dev/null
done
echo "Done."
query="*91\r\n\$3\r\ndel\r\n\$5\r\nkey10\r\n\$5\r\nkey11\r\n\$5\r\nkey12\r\n\$5\r\nkey13\r\n\$5\r\nkey14\r\n\$5\r\nkey15\r\n\$5\r\nkey16\r\n\$5\r\nkey17\r\n\$5\r\nkey18\r\n\$5\r\nkey19\r\n\$5\r\nkey10\r\n\$5\r\nkey11\r\n\$5\r\nkey12\r\n\$5\r\nkey13\r\n\$5\r\nkey14\r\n\$5\r\nkey15\r\n\$5\r\nkey16\r\n\$5\r\nkey17\r\n\$5\r\nkey18\r\n\$5\r\nkey19\r\n\$5\r\nkey10\r\n\$5\r\nkey11\r\n\$5\r\nkey12\r\n\$5\r\nkey13\r\n\$5\r\nkey14\r\n\$5\r\nkey15\r\n\$5\r\nkey16\r\n\$5\r\nkey17\r\n\$5\r\nkey18\r\n\$5\r\nkey19\r\n\$5\r\nkey10\r\n\$5\r\nkey11\r\n\$5\r\nkey12\r\n\$5\r\nkey13\r\n\$5\r\nkey14\r\n\$5\r\nkey15\r\n\$5\r\nkey16\r\n\$5\r\nkey17\r\n\$5\r\nkey18\r\n\$5\r\nkey19\r\n\$5\r\nkey10\r\n\$5\r\nkey11\r\n\$5\r\nkey12\r\n\$5\r\nkey13\r\n\$5\r\nkey14\r\n\$5\r\nkey15\r\n\$5\r\nkey16\r\n\$5\r\nkey17\r\n\$5\r\nkey18\r\n\$5\r\nkey19\r\n\$5\r\nkey10\r\n\$5\r\nkey11\r\n\$5\r\nkey12\r\n\$5\r\nkey13\r\n\$5\r\nkey14\r\n\$5\r\nkey15\r\n\$5\r\nkey16\r\n\$5\r\nkey17\r\n\$5\r\nkey18\r\n\$5\r\nkey19\r\n\$5\r\nkey10\r\n\$5\r\nkey11\r\n\$5\r\nkey12\r\n\$5\r\nkey13\r\n\$5\r\nkey14\r\n\$5\r\nkey15\r\n\$5\r\nkey16\r\n\$5\r\nkey17\r\n\$5\r\nkey18\r\n\$5\r\nkey19\r\n\$5\r\nkey20\r\n\$5\r\nkey21\r\n\$5\r\nkey22\r\n\$5\r\nkey23\r\n\$5\r\nkey24\r\n\$5\r\nkey25\r\n\$5\r\nkey26\r\n\$5\r\nkey27\r\n\$5\r\nkey28\r\n\$5\r\nkey29\r\n\$5\r\nkey30\r\n\$4\r\nkey9\r\n\$4\r\nkey8\r\n\$4\r\nkey7\r\n\$4\r\nkey6\r\n\$4\r\nkey5\r\n\$4\r\nkey4\r\n\$4\r\nkey3\r\n\$4\r\nkey2\r\n\$4\r\nkey1\r\n"

expected=":30"
res=$(printf "$query" | ../yarmnc 127.0.0.1 11311 | tr -d '\r\n')

if [ $res != $expected ]; then
  printf "\033[33mFail $res.\033[0m\r\n"
  exit 1
else
  printf "\033[32mPass $res.\033[0m\r\n"
fi

