# cat ./x.data | nc 127.0.0.1 11311 
gunzip -c ./x.data.gz | nc 127.0.0.1 11311 > x.tmp
cat x.tmp

expected=$(gunzip -c x.data.gz |  grep "^*" | wc -l)
count=$(cat x.tmp | wc -l | awk '{print $1}')
if [ $count -eq $expected ];
then
  echo -e "\033[32mSuccess $count/$expected.\033[0m"
  exit 0
else
  echo -e "\033[33mFail $count/$expected.\033[0m"
  exit 1
fi

#while true; do ./x.sh ; if [ $? -ne 0 ]; then break; fi; sleep 0.02; date; done
