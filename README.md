[![BookMyTime](http://bookmytime.dev/api/project/exXbREFsgPwmOAqfSeRM/button)](http://bookmytime.dev/book/exXbREFsgPwmOAqfSeRM)
               
Cacheismo is a sciptable object cache which can be used as replacement for memcached
and redis. It supports memcache protocol (tcp and ascii only). 

- The best part about using cacheismo is that is it fully scriptable in lua.
  Sample objects map, set, quota and sliding window counters written in lua can 
  be found in scripts directory. You could create your own objects and place them
  in the scripts directory. 
  
  The interface for accessing scriptable objects is implemented via memcached 
  get requests. For example: 
  get set:new:mykey     - would create a new set object refered via myKey
  get set:put:myKey:a   - would put key a in the set myKey
  get set:count:myKey   - would return number of elements in the set
  get set:union:myKey1:myKey2   - would return union of sets myKey1 and myKey2
  
  See scripts/set.lua for other functions.  
  
  I call this approach virtual keys. Reason for using it .. you can use 
  existing memcached libraries to access cacheismo.
  See
  http://chakpak.blogspot.com/2011/09/cacheimso-sliding-window-counter.html
  http://chakpak.blogspot.com/2011/09/rate-limitingquota-with-cacheismo.html
  http://chakpak.blogspot.com/2011/09/cacheismo-lua-api.html
  
Others reasons why you might want to use it.
- Slab allocator used in memcache would waste memory if slab sizes and object
  sizes are not in sync. For example when using slab sizes of 64/128/256/512/1024 bytes
  object with size 513 is stored in 1024 bytes slab, wasting almost half the 
  memory. cashismo tries to maintain memory used by object as close as possible 
  to the object size. No configuration is required, no tuning of slab sizes.  
  See http://chakpak.blogspot.com/2011/09/cacheismo-memory-allocation.html

- True LRU support. memcache will evict items from the slab in which object will 
  be placed. What this means is that memcache will remove objects from cache 
  which are not truly the LRU objects in the cache. cachismo will always use 
  the LRU, irrespective of the size. If sizes of objects in the cache is not 
  fixed, it is either hard to estimate or changes over time, then you would see 
  much better HIT rate with cacheismo compared to memcached.

- Cluster Support 
  Cacheismo uses virtual keys. This breaks the consistent hashing algo because 
  hash of virtual key is not likely to be the hash of actual data key. Cacheismo 
  provides server side consistent hashing. Which means if you connect to arbitrary 
  cacheismo server and throw any random key, it will find out the actual server 
  and route the request to that server. This functionality is suported but not 
  enabled by default. Cacheismo provide "consistent" object type which can be 
  configured with set of servers and then one could ask which is the server 
  given a key. Usage is optional. 
  Cacheismo also supports parallel get operations on multiple cacheismo severs.
  This can be used to provide map-reduce like functionality.
  See 
  http://chakpak.blogspot.com/2011/10/using-cacheismo-cluster-for-real-time.html
  http://chakpak.blogspot.com/2011/10/cacheismo-cluster-update-2.html
  http://chakpak.blogspot.com/2011/10/finding-keys-in-cacheismo.html

Building - cacheismo depends on libevent and lua (luagit can also be used) 
         - intsall libevent and lua if not already present on your system.
         - ./configure; make 
         - binary is created in the src directory.
  
It is single threaded. Run multiple instances on multicore systems.

Introduction: http://chakpak.blogspot.com/2011/09/introducing-cacheismo.html
Disuss      : cacheismo@googlegroups.com 
