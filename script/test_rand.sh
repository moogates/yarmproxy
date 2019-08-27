for size in `seq 1 100000`; do
  ./set.sh 0
  ./get.sh
  sleep 0.02
done
#while true; do ./get.sh; sleep 0.02; done
