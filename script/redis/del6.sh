printf "Setup begin ..."
for id in `seq 1 100`; do
  key=key$id
  ./marshal_set $key 2027 | nc 127.0.0.1 11311 > /dev/null
done
echo "Done."
#printf "$query" | nc 127.0.0.1 6379
gunzip -c del6.data.gz | nc 127.0.0.1 11311 > del6.tmp

cat del6.tmp

expected=":90"
res=$(cat del6.tmp | tr -d '\r\n')

if [ $res == $expected ]; then
  echo -e "\033[32mPass.\033[0m"
  exit 0
else
  echo -e "\033[33mFail.\033[0m"
  exit 1
fi
