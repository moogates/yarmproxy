printf "add counter1 0 0 1\r\n0\r\n" | nc 127.0.0.1 11311
printf "incr counter1 1\r\n" | nc 127.0.0.1 11311
