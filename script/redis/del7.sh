echo "Setup begin ..."
for id in `seq 1 100`; do
  key=key$id
  ./data_gen $key 2027 | nc 127.0.0.1 11311 > /dev/null
done
echo "Setup done."
gunzip -c del7.data.gz | nc 127.0.0.1 11311 > del7.tmp

cat del7.tmp

expected=":90"
res=$(cat del7.tmp | tr -d '\r\n')

if [ $res == $expected ]; then
  echo -e "\033[32mPass.\033[0m"
  exit 0
else
  echo -e "\033[33mFail.\033[0m"
  exit 1
fi
