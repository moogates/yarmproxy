for id in `seq 1 15`; do
  echo "./mset${id}.sh"
  ./mset${id}.sh
  sleep 0.1
done

for id in `seq 1 3`; do
  echo "./mget_pipeline_${id}.sh"
  ./mget_pipeline_${id}.sh
  sleep 0.1
done
