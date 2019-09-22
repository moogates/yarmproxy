res=$(cat ./mset3.data | nc 127.0.0.1 11311 | head -n1)
echo $res
exit

if [ $? -ne 0 ]; then
  echo -e "\033[33m fail \033[0m"
else
  echo -e "\033[32m success \033[0m"
fi
echo
