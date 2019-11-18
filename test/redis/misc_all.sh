
YARMPROXY_PORT=11311
if [ $# -gt 0 ]; then
  YARMPROXY_PORT=$1
fi

for script in append1.sh decr1.sh decrby1.sh exists1.sh exists2.sh getrange1.sh \
              getset1.sh incr1.sh incrby1.sh incrbyfloat1.sh psetex1.sh strlen1.sh \
              touch1.sh touch2.sh ttl1.sh ; do
  echo -e "./$script $YARMPROXY_PORT"
  ./$script $YARMPROXY_PORT
  if [ $? -ne 0 ]; then
    echo "test fail on ./$script"
    exit 1
  fi
  echo
done

