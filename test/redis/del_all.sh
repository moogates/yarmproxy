for id in `seq 2 10`; do
  echo "./del${id}.sh"
  ./del${id}.sh
  sleep 0.01
done
./del_pipeline_1.sh