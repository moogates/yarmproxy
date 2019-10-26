for id in `seq 1 1000000`; do
  body_size=$(echo "($RANDOM*23+2027)%262144" | bc)
  count=$(echo "($RANDOM*23+2027)%128" | bc)
  echo body_size=$body_size count=$count
  ./marshal_mset key $body_size $count | ../yarmnc 127.0.0.1 11311
  echo $id
  if [ $? -ne 0 ]; then
    break
  fi
  sleep 0.02
done

