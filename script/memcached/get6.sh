body_size=$(echo "($RANDOM*23+2027)%262144" | bc)
for id in `seq 1 8`; do
  key=key$id
  #./data_gen $body_size | sed "1s/EXAMPLE_KEY/$key/g" | nc 127.0.0.1 11311 > /dev/null
  ./data_gen $body_size | sed "1s/EXAMPLE_KEY/$key/g" | nc 127.0.0.1 11311
done

query="get key1 key2 key3 key4 key5 key6 key7 key8\r\nget key1 key2 key3 key4 key5 key6 key7 key8\r\nget key1 key2 key3 key4 key5 key6 key7 key8\r\nget key1 key2 key3 key4 key5 key6 key7 key8\r\nget key1 key2 key3 key4 key5 key6 key7 key8\r\nget key1 key2 key3 key4 key5 key6 key7 key8\r\nget key1 key2 key3 key4 key5 key6 key7 key8\r\nget key1 key2 key3 key4 key5 key6 key7 key8\r\nget key1 key2 key3 key4 key5 key6 key7 key8\r\nget key1 key2 key3 key4 key5 key6 key7 key8\r\nget key1 key2 key3 key4 key5 key6 key7 key8\r\nget key1 key2 key3 key4 key5 key6 key7 key8\r\nget key1 key2 key3 key4 key5 key6 key7 key8\r\nget key1 key2 key3 key4 key5 key6 key7 key8\r\nget key1 key2 key3 key4 key5 key6 key7 key8\r\nget key1 key2 key3 key4 key5 key6 key7 key8\r\nget key1 key2 key3 key4 key5 key6 key7 key8\r\nget key1 key2 key3 key4 key5 key6 key7 key8\r\nget key1 key2 key3 key4 key5 key6 key7 key8\r\nget key1 key2 key3 key4 key5 key6 key7 key8\r\n"
printf "$query" | nc 127.0.0.1 11311 | grep "VALUE\|END" > get6.tmp

expected_value_lines=160
value_lines=$(cat get6.tmp | grep -c $body_size)

if [ $value_lines -ne $expected_value_lines ]; then
  echo -e "\033[33mResponse VALUE lines error:$value_lines/$expected_value_lines.\033[0m"
  exit 1
else
  echo -e "\033[32mResponse VALUE lines ok.\033[0m"
fi

expected_end_lines="9 18 27 36 45 54 63 72 81 90 99 108 117 126 135 144 153 162 171 180 "
end_lines=$(cat get6.tmp | grep -n END | awk -F: '{print $1}' | tr '\r\n' ' ')
echo "$end_lines"
if [ "$end_lines" != "$expected_end_lines" ]; then
  echo -e "\033[33mFail: Response END lines error.\033[0m"
  exit 1
else
  echo -e "\033[32mResponse END lines ok.\033[0m"
fi

echo -e "\033[32mPass.\033[0m"
echo

