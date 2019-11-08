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
  set
  get
  mget

  get
  getset
  getrange
  ttl
  incr
  incrby
  incrbyfloat
  decr
  decrby
  strlen

  set
  append
  setrange
  setnx
  psetex
  setex

  mset(*non-atomic)
  mget

  del
  exists
  touch

  yarmstats  // show the yarmproxy statistics

## Memcached
  (more to be supported...)
  get
  gets

  set
  add
  replace
  append
  prepend
  cas

  delete
  incr
  decr
  touch

  yarmstats  // show the yarmproxy statistics

# File Structure
  - `proxy` the yarmproxy source code
  - `unit` unit test
  - `test` testing redis/memcached commands 
  - `stress` a client for stress testing
  - `benchmark` benchmarking origin_server(redis or memcached)/yarmproxy/nutcracker

# Build
## Dependencies
boost.asio (boost 1.56 or higher recommended)

## Linux

## MAX OSX

## Windows

# Contact
  Send mail to moogates@163.com if you have any questions or found any bugs.


