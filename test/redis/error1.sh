query="*3\r\n\$6\r\nxxxset\r\n\$4\r\nkey1\r\n\$6\r\nvalue1\r\n"

expected="-ERR YarmProxy unsupported redis command:[xxxset]"
res=$(printf "$query" | ../yarmnc 127.0.0.1 11311 | tr -d '\r\n')

if [ "$res" == "$expected" ]; then
  printf "\033[32mPass \"$res\".\033[0m\r\n"
else
  printf "\033[33mFail \"$res\".\033[0m\r\n"
  exit 1
fi

