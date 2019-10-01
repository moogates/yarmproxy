for seq in `seq 1 100000`; do
  body_size=$(echo "($RANDOM*23+2027)%262144" | bc)
  echo "round $seq bod_size=$body_size"
  ./set.sh $body_size
  ./get3.sh | head
  ./mget4.sh | head
  ./mset15.sh
  sleep 0.13
done
#while true; do ./get.sh; sleep 0.02; done
