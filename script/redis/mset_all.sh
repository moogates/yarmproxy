for id in `seq 1 15`; do
  echo "./mset${id}.sh"
  ./mset${id}.sh
  sleep 0.1
done

for id in `seq 1 7`; do
  echo "./mset_pipeline_${id}.sh"
  ./mset_pipeline_${id}.sh
  sleep 0.1
done
