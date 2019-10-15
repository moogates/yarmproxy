for id in `seq 1 4`; do
  echo "./mget${id}.sh"
  ./mget${id}.sh
  sleep 0.1
done

for id in `seq 1 3`; do
  echo "./mget_pipeline_${id}.sh"
  ./mget_pipeline_${id}.sh
  sleep 0.1
done
