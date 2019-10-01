always_run=0
if [ $# -eq 0 ]; then
  echo "Usage: $0 script_file [always_run=0]"
  exit 1
fi
if [ $# -gt 1 ]; then
  always_run=$2
fi

script=$1
echo "run script $script"

for i in `seq 1 10000`; do
  ./$script
  if [ $? -ne 0 ] && [ $always_run -eq 0 ]; then
    echo "error on round $i"
    break
  fi
  sleep 0.02
  echo $i
done
