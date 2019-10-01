gunzip -c ./mset16.data.gz | nc 127.0.0.1 11311 > mset16.tmp

cat mset16.tmp

expected=6
res=$(cat mset16.tmp | wc -l | awk '{print $1}')

if [ $res -eq $expected ]; then
  echo -e "\033[32mSuccess $res/$expected.\033[0m"
  exit 0
else
  echo -e "\033[33mFail $res/$expected.\033[0m"
  exit 1
fi

