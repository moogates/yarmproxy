while true; do
  ./set2.sh | grep OK

  if [ $? -eq 0 ]; then 
    echo "ok"
  else
    break
  fi

  sleep 0.02
  date
done
