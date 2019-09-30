printf "add counter2 0 0 1\r\n9\r\n" | nc 127.0.0.1 11311
printf "decr counter2 1\r\n" | nc 127.0.0.1 11311
printf "incr counter2 5\r\n" | nc 127.0.0.1 11311
