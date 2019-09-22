size=2027
if [ $# -gt 0 ]; then
  size=$1
fi

for id in `seq 1 9`; do
  key=key$id
  echo $key
  # 存储命令: <command name> <key> <flags> <exptime> <bytes>
  # ./data_gen $key $size | nc 127.0.0.1 6379
  ./data_gen $key $size | nc 127.0.0.1 11311
done
