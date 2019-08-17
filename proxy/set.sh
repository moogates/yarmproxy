# printf "set key 0 10 6\r\nresult\r\n" | nc 127.0.0.1 11311
#for id in `seq 1 10`; do
for id in `seq 1 1`; do
  key=key$id
  echo $key
  # 存储命令: <command name> <key> <flags> <exptime> <bytes>
  printf "set $key 0 86400 6\r\nresult\r\n" | nc 127.0.0.1 11311
done
