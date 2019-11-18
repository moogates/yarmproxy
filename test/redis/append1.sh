printf "*2\r\n\$3\r\ndel\r\n\$4\r\nk001\r\n" | ../yarmnc 127.0.0.1 11311 > /dev/null

query="*3\r\n\$6\r\nappend\r\n\$4\r\nk001\r\n\$6\r\nvalue1\r\n"

expected=":6"
res=$(printf "$query" | ../yarmnc 127.0.0.1 11311 | tr -d '\r\n')
if [ $res != $expected ]; then
  printf "\033[33mFail 1.\033[0m\r\n"
  exit 1
fi

expected=":12"
res=$(printf "$query" | ../yarmnc 127.0.0.1 11311 | tr -d '\r\n')
if [ $res != $expected ]; then
  printf "\033[33mFail 2.\033[0m\r\n"
  exit 2
fi

printf "\033[32mPass $res.\033[0m\r\n"

