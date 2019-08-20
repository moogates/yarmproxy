while true; do ./large_set.sh; ./get.sh | grep "VALUE\|END"; ./set.sh; ./get.sh; sleep 0.2; done
