file_count=$(echo "1+$RANDOM%30" | bc)
data_files=$(ls *.data.gz | awk  -F"\3" 'BEGIN{srand();}{value=int(rand()*10000000); print value"\3"$0 }' | sort | awk -F"\3" '{print $2}' | head -n$file_count)
echo $data_files
gunzip -c $data_files >  shuffle.data.tmp

driver=nc
driver=../yarmnc

cat shuffle.data.tmp | $driver 127.0.0.1 11311 | grep -v "^00"
#cat shuffle.data.tmp | nc 127.0.0.1 11311
#cat shuffle.data.tmp | $driver 127.0.0.1 6379 | grep -v "^00"
#cat shuffle.data.tmp | nc 127.0.0.1 6379

