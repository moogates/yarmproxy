for id in `seq 2 9`; do
  echo "./set${id}.sh"
  ./set${id}.sh
  sleep 0.01
done
