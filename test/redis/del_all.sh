YARMPROXY_PORT=11311
if [ $# -gt 0 ]; then
  YARMPROXY_PORT=$1
fi

for script in del2.sh del3.sh del4.sh del5.sh del6.sh del7.sh del8.sh del9.sh del10.sh del_pipeline_1.sh ; do
  echo -e "./$script $YARMPROXY_PORT"
  ./$script $YARMPROXY_PORT
  if [ $? -ne 0 ]; then
    echo "test fail on ./$script"
    exit 1
  fi
  echo
done

