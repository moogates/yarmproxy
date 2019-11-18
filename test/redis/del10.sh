printf "Setup begin ..."
for id in `seq 1 100`; do
  key=key$id
  ./marshal_set $key 2027 | ../yarmnc 127.0.0.1 11311 > /dev/null
done
echo "Done."

expected=":90"
res=$(gunzip -c  del10.data.gz | ../yarmnc 127.0.0.1 11311 | tr -d '\r\n')

if [ $res != $expected ]; then
  printf "\033[33mFail $res.\033[0m\r\n"
  exit 1
else
  printf "\033[32mPass $res.\033[0m\r\n"
fi
