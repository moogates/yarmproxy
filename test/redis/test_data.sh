
if [ $# -eq 0 ]; then
  echo "Usage: $0 data_file"
  exit 1
fi

data_file=$1
echo "test data $data_file"

cat $data_file | nc 127.0.0.1 11311 

exit

for i in `seq 1 1000`; do
  ./$script
  if [ $? -ne 0 ]; then
    echo "error on round $i"
    break
  fi
  sleep 0.02
  date
done
