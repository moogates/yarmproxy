for id in `seq 1 6`; do
  echo "./set${id}.sh"
  ./set${id}.sh
  sleep 0.01
done
