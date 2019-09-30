while true; do
  port=$(echo "11211+$RANDOM%5" | bc)
  echo $port

  pid=$(ps axu | grep memcached.*$port$ | awk {'print $2'})
  echo "kill memcached on port $port (pid=$pid)"

  kill -9 $pid

  sleep 1.6

  memcached -d -umuyuwei -t4 -p$port

  sleep 0.5

  pid=$(ps axu | grep memcached.*$port$ | awk {'print $2'})
  echo "restart memcached on port $port (pid=$pid)"
  sleep 1.5
  echo 
done
