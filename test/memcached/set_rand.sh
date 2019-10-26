rm -fv set_rand.tmp

set_count=$(echo "($RANDOM*23+2027)%200" | bc)
for i in `seq 1 $set_count`; do
  body_size=$(echo "($RANDOM*23+2027)%262144" | bc)
  key="key${i}"
  # echo $key $body_size
  ./marshal_set $key $body_size 86400 >> set_rand.tmp
done

stored_count=$(cat set_rand.tmp | ../yarmnc | grep -c "STORED")

if [ $stored_count -ne $set_count ]; then
  echo -e "\033[33mFail: set $stored_count/$set_count.\033[0m"
  exit 1
else
  echo -e "\033[32mPass: set $stored_count/$set_count.\033[0m"
fi
