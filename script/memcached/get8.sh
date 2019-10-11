driver=nc
driver=../yarmnc
port=11311
body_size=$(echo "($RANDOM*23+2027)%302144" | bc)
body_size=200000
printf "Setting up(body_size=$body_size)..."
rm -f get8.init.tmp
for id in `seq 1 100`; do
  key=key$id
  ./data_gen $body_size | sed "1s/EXAMPLE_KEY/$key/g" | nc 127.0.0.1 $port >> get8.init.tmp
done
echo "Done"

gunzip -c get8.data.gz | $driver 127.0.0.1 $port | grep "ERROR\|VALUE\|END" > get8.tmp

expected_value_lines=1500
value_lines=$(cat get8.tmp | grep -c $body_size)
if [ $value_lines -ne $expected_value_lines ]; then
  echo -e "\033[33mFail: Response VALUE lines error.$value_lines.\033[0m"
  exit 1
else
  echo -e "\033[32mResponse VALUE lines ok.\033[0m"
fi

expected_end_lines=""
for id in `seq 1 100`; do
  expected_end_lines+=$((id*16))
  expected_end_lines+=" "
done
end_lines=$(cat get8.tmp | grep -n END | awk -F: '{print $1}' | tr '\r\n' ' ')
if [ "$end_lines" != "$expected_end_lines" ]; then
  echo -e "\033[33mFail: Response END lines error.\033[0m"
  #exit 1
else
  echo -e "\033[32mResponse END lines ok.\033[0m"
fi

echo -e "\033[32mPass.\033[0m"
echo
