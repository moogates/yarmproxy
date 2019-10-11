query="set key15 0 86400 6\r\nresult\r\nset key25 0 86400 6\r\nresult\r\nset key55 0 86400 6\r\nresult\r\nset key35 0 86400 6\r\nresult\r\nset key5 0 86400 6\r\nresult\r\nset key65 0 86400 6\r\nresult\r\nset key85 0 86400 6\r\nresult\r\nset key1 0 86400 6\r\nresult\r\nset key3 0 86400 6\r\nresult\r\nset key2 0 86400 6\r\nresult\r\nset key4 0 86400 6\r\nresult\r\n"
printf "$query" | nc 127.0.0.1 11311 > set2.tmp
count=$(cat set2.tmp | grep -c "^ERROR\|^STORED")
expected=11

if [ $count -ne $expected ]; then
  echo -e "\033[33mFail:$count/$expected.\033[0m"
  exit 1
else
  echo -e "\033[32mPass:$count/$expected.\033[0m"
fi
