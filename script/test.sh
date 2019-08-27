for size in `seq 1 100000`; do
  ./set.sh $size
  ./get.sh
  sleep 0.02
done
#while true; do ./get.sh; sleep 0.02; done
