size=2027
if [ $# -gt 0 ]; then
  size=$1
fi

query="*3\r\n\$4\r\nmset\r\n\$4\r\nkey1\r\n\$6\r\nvalue1\r\n"
query="*7\r\n\$4\r\nmset\r\n\$4\r\nkey1\r\n\$6\r\nvalue1\r\n\$4\r\nkey8\r\n\$6\r\nvalue8\r\n\$4\r\nkey7\r\n\$6\r\nvalue7\r\n"
echo "--------- $query -----------"
printf "$query" | nc 127.0.0.1 11311
#printf "$query" | nc 127.0.0.1 6379
echo
exit

#query="set key15 0 86400 6\r\nresult\r\nset key25 0 86400 6\r\nresult\r\nset key55 0 86400 6\r\nresult\r\nset key35 0 86400 6\r\nresult\r\nset key5 0 86400 6\r\nresult\r\nset key65 0 86400 6\r\nresult\r\nset key85 0 86400 6\r\nresult\r\nset key1 0 86400 6\r\nresult\r\nset key3 0 86400 6\r\nresult\r\nset key2 0 86400 6\r\nresult\r\nset key4 0 86400 6\r\nresult\r\n"
#echo "--------- $query -----------"
#printf "$query" | nc 127.0.0.1 11311
echo

for id in `seq 1 9`; do
  key=key$id
  echo $key
  # 存储命令: <command name> <key> <flags> <exptime> <bytes>
  # ./marshal_set $key $size | nc 127.0.0.1 6379
  ./marshal_set $key $size | nc 127.0.0.1 11311
done
