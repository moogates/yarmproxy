for len in `seq 10 100`; do
  ./marshal_set key1 $len | ../yarmnc 127.0.0.1 $YARMPROXY_PORT > /dev/null

  res=$(printf "*2\r\n\$6\r\nstrlen\r\n\$4\r\nkey1\r\n" | ../yarmnc 127.0.0.1 11311 | tr -d ':\r\n')
  if [ $res != $len ]; then
    printf "\033[33mFail $res.\033[0m\r\n"
    exit 1
  fi
done

printf "\033[32mPass.\033[0m\r\n"

