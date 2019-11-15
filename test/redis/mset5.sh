YARMPROXY_PORT=11311
if [ $# -gt 0 ]; then
  YARMPROXY_PORT=$1
fi

expected="+OK"
res=$(gunzip -c ./mset5.data.gz | ../yarmnc 127.0.0.1 $YARMPROXY_PORT | tr -d '\r\n')

if [ $res != $expected ]; then
  echo -e "\033[33mFail $res.\033[0m"
  exit 1
else
  echo -e "\033[32mPass $res.\033[0m"
fi

