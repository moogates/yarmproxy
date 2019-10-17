body_size=$(echo "($RANDOM*23+2027)%262144" | bc)
printf "Setting up(body_size=$body_size)..."
for id in `seq 1 10`; do
  key=key$id
  ./marshal_set $key $body_size | nc 127.0.0.1 11311 > /dev/null
done
echo "Done"

query="get key1 key2 key3 key4 key5 key6 key7 key8\r\nget key1 key2 key3 key4 key5 key6 key7 key8 key9 key10\r\nget key1 key2 key3 key4 key5 key6 key7 key8\r\n"
printf "$query" | nc 127.0.0.1 11311 | grep "^ERROR\|^VALUE\|^END" > get5.tmp
cat get5.tmp

expected_value_lines=26
value_lines=$(cat get5.tmp | grep -c $body_size)
if [ $value_lines -ne $expected_value_lines ]; then
  echo -e "\033[33mFail: Response VALUE lines error.$value_lines/$expected_value_lines.\033[0m"
  exit 1
else
  echo -e "\033[32mResponse VALUE lines ok.\033[0m"
fi

expected_end_lines="9 20 29 "
end_lines=$(cat get5.tmp | grep -n END | awk -F: '{print $1}' | tr '\r\n' ' ')
echo "$end_lines"
if [ "$end_lines" != "$expected_end_lines" ]; then
  echo -e "\033[33mFail: Response END lines error.\033[0m"
  exit 1
else
  echo -e "\033[32mResponse END lines ok.\033[0m"
fi

echo -e "\033[32mPass.\033[0m"
echo

