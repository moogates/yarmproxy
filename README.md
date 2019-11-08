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
  (more to be supported...)
  append
  decr
  decrby
  del
  exists
  get
  getrange
  getset
  incr
  incrby
  incrbyfloat
  mget
  mset(non-atomic)
  psetex
  set
  setex
  setnx
  setrange
  strlen
  touch
  ttl
  yarmstats(show the yarmproxy statistics)

## Memcached
  (more to be supported...)
  add
  append
  cas
  decr
  delete
  get
  gets
  incr
  prepend
  replace
  set
  touch
  yarmstats(show the yarmproxy statistics)

# File Structure
  - `proxy` the yarmproxy source code
  - `unit` unit test
  - `test` testing redis/memcached commands 
  - `stress` a client for stress testing
  - `benchmark` benchmarking origin_server(redis or memcached)/yarmproxy/nutcracker

# Build
## Dependencies
boost.asio (boost 1.56 or higher recommended)

Tested Enviroment
## CentOS
  CentOS Linux release 7.6.1810 (Core)
  g++ 4.8.5 / clang++ 3.4.2

## Ubuntu

## Mac OS X
  Mac OS X 10.15
  Apple clang version 11.0.0 (clang-1100.0.33.12)

## Windows
  TODO
  Microsoft Visual C++ 2017

# Contact
  Send mail to moogates@163.com if you have any questions or found any bugs.
  Thanks a lot for your time in advance.


