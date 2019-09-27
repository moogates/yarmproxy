driver=nc
driver=../yarmnc

#gunzip -c ./x.data.gz | $driver 127.0.0.1 11311
#exit
gunzip -c ./mset_pipeline_6.data.gz | $driver 127.0.0.1 11311 > mset_pipeline_6.tmp
cat mset_pipeline_6.tmp

expected=$(gunzip -c mset_pipeline_6.data.gz |  grep "^*" | wc -l | awk '{print $1}')
count=$(cat mset_pipeline_6.tmp | wc -l | awk '{print $1}')
if [ $count -eq $expected ];
then
  echo -e "\033[32mSuccess $count/$expected.\033[0m"
  exit 0
else
  echo -e "\033[33mFail $count/$expected.\033[0m"
  exit 1
fi

#while true; do ./x.sh ; if [ $? -ne 0 ]; then break; fi; sleep 0.02; date; done
