printf "add counter1 0 0 3\r\n100\r\n" | nc 127.0.0.1 11311
printf "incr counter1 10\r\ndecr counter1 1\r\ndecr counter1 1\r\ndecr counter1 1\r\ndecr counter1 1\r\ndecr counter1 1\r\ndecr counter1 1\r\ndecr counter1 1\r\ndecr counter1 1\r\ndecr counter1 1\r\ndecr counter1 1\r\n" | nc 127.0.0.1 11311
#printf "incr counter2 5\r\n" | nc 127.0.0.1 11311
