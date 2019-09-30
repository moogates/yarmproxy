./data_gen 2000000 | sed "1s/EXAMPLE_KEY/key1/g" | ../yarmnc 127.0.0.1 11311
# gunzip -c set4.data.gz | nc 127.0.0.1 11211
echo
