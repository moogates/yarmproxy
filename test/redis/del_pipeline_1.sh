printf "Setup begin ..."
for id in `seq 1 100`; do
  key=key$id
  ./marshal_set $key 2027 | nc 127.0.0.1 11311 > /dev/null
done
echo "Done."

gunzip -c ./del6.data.gz ./del7.data.gz ./del8.data.gz ./del9.data.gz ./del10.data.gz > del_pipeline_1.data.tmp
cat del_pipeline_1.data.tmp | nc 127.0.0.1 11311 > del_pipeline_1.tmp
#cat del_pipeline_1.tmp

expected=$(cat del_pipeline_1.data.tmp | grep "^*" | wc -l | awk '{print $1}')
res=$(cat del_pipeline_1.tmp | wc -l | awk '{print $1}')

if [ $res -eq $expected ]; then
  printf "\033[32mPass $res/$expected.\033[0m\r\n"
  exit 0
else
  printf "\033[33mFail $res/$expected.\033[0m\r\n"
  exit 1
fi

