size=2027
if [ $# -gt 0 ]; then
  size=$1
fi

for id in `seq 1 100`; do
  key=key$id
  echo $key
  printf "get $key\r\n" | nc 127.0.0.1 11311 | grep "VALUE\|END"
done
