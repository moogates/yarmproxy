cd ../script/redis
for body_size in 50 200 500 2000 5000 10000 20000 50000 100000 200000 500000; do
  for port in 11311 22121;do
    echo "test mget body_size=$body_size port=$port"
    ./set_100.sh $body_size $port > /dev/null 2>&1
    time for id in `seq 1 100`; do ./mget1.sh $port > /dev/null 2>&1; done
  done
done

