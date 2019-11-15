./marshal_set key1 1024 86400 1

./marshal_mset key 1024 32
./marshal_mset key 300000 64 | ../yarmnc 127.0.0.1 11311
