cd memcached
./set_all.sh
./get_all.sh
cd ..

cd redis
./set_all.sh
./get_all.sh
./mset_all.sh
./mget_all.sh
cd ..
