gunzip -c set5.data.gz | nc 127.0.0.1 11311 > set5.tmp
stored_count=$(cat set5.tmp | grep -c STORED)
set_count=$(gunzip -c set5.data.gz | grep -c "^set ")

if [ $stored_count -ne $set_count ]; then
  echo -e "\033[33mFail: set $stored_count/$set_count.\033[0m"
  exit 1
else
  echo -e "\033[32mPass: set $stored_count/$set_count.\033[0m"
fi
