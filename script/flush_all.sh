

# 小心操作，会清空缓存!

printf "This command will remove all data in memcached. Are you sure?(Y/N) "
read line
if [ $line != "Y" ] && [ $line != "y" ];then
  echo "You give up flushing"
  exit
fi

printf "flush_all\r\n" | nc 127.0.0.1 11211
printf "flush_all\r\n" | nc 127.0.0.1 11212
printf "flush_all\r\n" | nc 127.0.0.1 11213
printf "flush_all\r\n" | nc 127.0.0.1 11214
printf "flush_all\r\n" | nc 127.0.0.1 11215
echo "Flushed."

