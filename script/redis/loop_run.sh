
if [ $# -eq 0 ]; then
  echo "Usage: $0 script_file"
  exit 1
fi

script=$1
echo "run script $script"

for i in `seq 1 10000`; do
  ./$script
  if [ $? -ne 0 ]; then
    echo "error on round $i"
    break
  fi
  sleep 0.02
  echo $i
done
