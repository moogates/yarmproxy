size=2027
if [ $# -gt 0 ]; then
  size=$1
fi

./marshal_mset key $size 100 | ../yarmnc
