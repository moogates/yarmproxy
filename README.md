# yarmproxy
yarmproxy(Yet Another Redis and Memcached PROXY) is a high performance redis/memcached proxy.


# Features
0. zero copy
1. multi-thread workers & lock-free
2. pipelined request processing
3. parallel multi-read/multi-write
4. automatic failover
5. portable to windows MSVC++

# Protocol Support
## Redis
  set
  mset(*non-atomic)
  get
  mget

## Memcached
  set
  add
  replace
  get

# Build
## boost-asio 1.56 or higher



