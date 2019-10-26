cd ../test/redis
round=0

for body_size in 50 200 500 2000 5000 10000 20000 50000 100000 200000 500000; do
#for body_size in 50 200 500 2000 5000 10000 20000 50000; do
  ./marshal_mset key 10000 100 > mset_req_${body_size}.tmp
  color=$((31+round%2))
  echo "Benchmarking redis mset, body_size $body_size"
  for server in "redis 6379" "yarmproxy 11311" "nutcracker 22121" ;do
    name=`echo $server | awk '{print $1}'`
    port=`echo $server | awk '{print $2}'`

    printf "\033[${color}m - Running against $name(port=$port) ... \033[0m"
    time for id in `seq 1 100`; do cat mset_req_${body_size}.tmp | ../yarmnc 127.0.0.1 $port > /dev/null 2>&1; done
  done
  round=$((round+1))
  echo
  echo
done

