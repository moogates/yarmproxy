# yarmproxy
yarmproxy(Yet Another Redis/Memcached PROXY) is a high performance redis/memcached proxy.


# Features
- very light-weighted and fast
- user space zero copy
- multi-thread workers & lock-free
- pipelined request processing
- parallel multi-read/multi-write
- automatic failover and best-effort return
- supported protocols: redis, memcached-text, and memcahced-binary
- portable to windows Linux/Mac/Windows

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

##Linux & Mac:
```
$ cd proxy
$ mkir build
$ cd build 
$ cmake ..
$ make
```

##Windows:
Supported soon...


#Testing
 please cd into the `test` dir

##Currently Tested Enviroment
### CentOS
  CentOS Linux release 7.6.1810 (Core)
  g++ 4.8.5 / clang++ 3.4.2

### Ubuntu

### Mac OS X
  Mac OS X 10.15
  Apple clang version 11.0.0 (clang-1100.0.33.12)

### Windows
  Supported soon...
  Microsoft Visual C++ 2017

#Benchmarking
 plz cd into the `benchmark` dir

# Contact
  Send mail to `moogates@163.com` if you have any questions or found any bugs.
  Thanks a lot for your time in advance.


