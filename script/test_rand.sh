for seq in `seq 1 100000`; do
  body_size=$(echo "($RANDOM*23+2027)%262144" | bc)
  echo "round $seq bod_size=$body_size"
  # ./set.sh $body_size
  ./set.sh 0
  ./get.sh
  # ./truncate_log.sh
done
#while true; do ./get.sh; sleep 0.02; done
