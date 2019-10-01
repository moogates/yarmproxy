while true; do
  ./mset15.sh | grep OK

  if [ $? -eq 0 ]; then 
    echo "ok"
  else
    echo "fail"
    break
  fi

  sleep 0.02
  date
done
