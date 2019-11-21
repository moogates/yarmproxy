command=memcached_set
script="$command.sh"

for round in `seq 1 10`; do
  result_file="$command.$round.out"
  echo "round $round $result_file"
  ./$script > $result_file 2>&1
  # ./$script 2>&1 | $result_file
  cat $result_file | grep -n "body_size [0-9]\+\|real.*$" -o | awk '{print $2}'
  echo
done
