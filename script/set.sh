size=2027
echo $#
if [ $# -gt 0 ]; then
  size=$1
fi
for id in `seq 1 100`; do
  key=key$id
  echo $key
  # 存储命令: <command name> <key> <flags> <exptime> <bytes>
  ./data_gen $size | sed "1s/EXAMPLE_KEY/$key/g" | nc 127.0.0.1 11311
done
