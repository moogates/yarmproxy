SERVER=redis-server
while true; do
  port=$(echo "6379+$RANDOM%3" | bc)
  echo $port

  pid=$(ps axu | grep ${SERVER}.*$port$ | awk {'print $2'})
  echo "kill ${SERVER} on port $port (pid=$pid)"

  kill -9 $pid

  sleep 1.6

  ${SERVER} ./redis_${port}.conf

  sleep 0.5

  pid=$(ps axu | grep ${SERVER}.*$port$ | awk {'print $2'})
  echo "restart ${SERVER} on port $port (pid=$pid)"
  sleep 1.5
  echo 
done
