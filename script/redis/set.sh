size=2027
if [ $# -gt 0 ]; then
  size=$1
fi

#query="*3\r\n\$3\r\nset\r\n\$4\r\nkey1\r\n\$6\r\nvalue1\r\n"
#echo "--------- $query -----------"
#printf "$query" | nc 127.0.0.1 11311
#printf "$query" | nc 127.0.0.1 6379
echo

#query="set key15 0 86400 6\r\nresult\r\nset key25 0 86400 6\r\nresult\r\nset key55 0 86400 6\r\nresult\r\nset key35 0 86400 6\r\nresult\r\nset key5 0 86400 6\r\nresult\r\nset key65 0 86400 6\r\nresult\r\nset key85 0 86400 6\r\nresult\r\nset key1 0 86400 6\r\nresult\r\nset key3 0 86400 6\r\nresult\r\nset key2 0 86400 6\r\nresult\r\nset key4 0 86400 6\r\nresult\r\n"
#echo "--------- $query -----------"
#printf "$query" | nc 127.0.0.1 11311
echo

for id in `seq 1 9`; do
  key=key$id
  echo $key
  # 存储命令: <command name> <key> <flags> <exptime> <bytes>
  # ./data_gen $key $size | nc 127.0.0.1 6379
  ./data_gen $key $size | nc 127.0.0.1 11311
done
