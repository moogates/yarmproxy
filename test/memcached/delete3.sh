rm -fv delete3.tmp

for i in `seq 1 30`; do
  key="key$i"
  printf "delete $key\r\n" >> delete3.tmp
done

cat delete3.tmp | ../yarmnc 127.0.0.1 11311

