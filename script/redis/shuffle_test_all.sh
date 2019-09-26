file_count=$(echo "1+$RANDOM%10" | bc)
data_files=$(ls *.data.gz | awk  -F"\3" 'BEGIN{srand();}{value=int(rand()*10000000); print value"\3"$0 }' | sort | awk -F"\3" '{print $2}' | head -n$file_count)
echo $data_files
gunzip -c $data_files >  shuffle.data.tmp
cat shuffle.data.tmp | nc 127.0.0.1 11311 | grep "^*\|*+"
#cat shuffle.data.tmp | nc 127.0.0.1 6379

