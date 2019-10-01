body_size=$(echo "($RANDOM*23+2027)%262144" | bc)
printf "Setting up(body_size=$body_size)..."
for id in `seq 1 10`; do
  key=key$id
  ./data_gen $body_size | sed "1s/EXAMPLE_KEY/$key/g" | nc 127.0.0.1 11311 > /dev/null
done
echo "Done"

gunzip -c get7.data.gz | nc 127.0.0.1 11311 | grep "VALUE\|END" > get7.tmp

expected_value_lines=350
value_lines=$(cat get7.tmp | grep -c $body_size)
if [ $value_lines -ne $expected_value_lines ]; then
  echo -e "\033[33mFail: Response VALUE lines error.$value_lines.\033[0m"
  exit 1
else
  echo -e "\033[32mResponse VALUE lines ok.\033[0m"
fi

expected_end_lines=""
for id in `seq 1 50`; do
  expected_end_lines+=$((id*8))
  expected_end_lines+=" "
done
echo "[$expected_end_lines]"
end_lines=$(cat get7.tmp | grep -n END | awk -F: '{print $1}' | tr '\r\n' ' ')
echo "$end_lines"
if [ "$end_lines" != "$expected_end_lines" ]; then
  echo -e "\033[33mFail: Response END lines error.\033[0m"
  exit 1
else
  echo -e "\033[32mResponse END lines ok.\033[0m"
fi

echo -e "\033[32mPass.\033[0m"
echo
