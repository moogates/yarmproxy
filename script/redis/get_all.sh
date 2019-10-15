for id in `seq 1 4`; do
  echo "./get${id}.sh"
  ./get${id}.sh
  sleep 0.01
done
