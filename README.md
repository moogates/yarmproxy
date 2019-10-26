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

# File Structure
  - `proxy` the yarmproxy source code
  - `unit` unit test
  - `test` testing redis/memcached commands 
  - `stress` a client for stress testing
  - `benchmark` benchmarking origin_server(redis or memcached)/yarmproxy/nutcracker

# Build
## Dependencies
boost-asio (boost 1.56 or higher recommended)

# Contact
  Send mail to moogates@163.com if you have any questions or found any bugs.


