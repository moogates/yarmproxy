gunzip -c ./mset3.data.gz | nc 127.0.0.1 11311 > mset3.tmp

cat mset3.tmp

expected="+OK"
res=$(cat mset3.tmp | tr -d '\r\n')

if [ $res == $expected ]; then
  echo -e "\033[32mSuccess \033[0m"
  exit 0
else
  echo -e "\033[33mFail \033[0m"
  exit 1
fi

