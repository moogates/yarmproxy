for script in set_all.sh get_all.sh mset_all.sh mget_all.sh del_all.sh misc_all.sh error_all.sh shuffle_test_all.sh; do
  ./$script
  if [ $? -ne 0 ]; then
    echo "test fail on ./$script"
    exit 1
  fi
done
