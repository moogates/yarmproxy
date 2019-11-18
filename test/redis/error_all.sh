
YARMPROXY_PORT=11311
if [ $# -gt 0 ]; then
  YARMPROXY_PORT=$1
fi

for script in error1.sh error2.sh spam1.sh spam2.sh spam3.sh ; do
  echo -e "./$script $YARMPROXY_PORT"
  ./$script $YARMPROXY_PORT
  if [ $? -ne 0 ]; then
    echo "test fail on ./$script"
    exit 1
  fi
  echo
done

