while true; do
  for id in `seq 1 15`; do
    echo "./mset${id}.sh"
    ./mset${id}.sh
    sleep 0.1
  done

  for id in `seq 1 5`; do
    echo "./mset_pipeline_${id}.sh"
    ./mset_pipeline_${id}.sh
    sleep 0.1
  done
  sleep 0.1
done