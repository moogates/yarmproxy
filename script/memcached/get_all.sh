 while true; do
  for id in `seq 1 9`; do
    echo "./get${id}.sh"
    ./get${id}.sh
    sleep 0.01
  done
  sleep 0.02
 done
