query="*3\r\n\$6\r\nappend\r\n\$4\r\nk001\r\n\$6\r\nvalue1\r\n"
printf "$query" | ../yarmnc 127.0.0.1 11311
