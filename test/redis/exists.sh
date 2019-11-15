query="*10\r\n\$6\r\nEXISTS\r\n\$4\r\nkey9\r\n\$4\r\nkey8\r\n\$4\r\nkey7\r\n\$4\r\nkey6\r\n\$4\r\nkey5\r\n\$4\r\nkey4\r\n\$4\r\nkey3\r\n\$4\r\nkey2\r\n\$4\r\nkey1\r\n"
#printf "$query" | ../yarmnc 127.0.0.1 6379
printf "$query" | ../yarmnc 127.0.0.1 11311
exit

