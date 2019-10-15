printf "Setup begin ..."
for id in `seq 1 100`; do
  key=key$id
  ./data_gen $key 2027 | nc 127.0.0.1 11311 > /dev/null
done
echo "Done."

gunzip -c  del10.data.gz | nc 127.0.0.1 11311 > del10.tmp
cat del10.tmp

expected=":90"
res=$(cat del10.tmp | tr -d '\r\n')

if [ $res == $expected ]; then
  echo -e "\033[32mPass.\033[0m"
  exit 0
else
  echo -e "\033[33mFail.\033[0m"
  exit 1
fi