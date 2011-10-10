bin_PROGRAMS = cacheismo
SUBDIRS = common cacheitem chunkpool datastream fallocator hashmap parser io cluster lua 

cacheismo_CPPFLAGS = -I$(top_srcdir)

cacheismo_LDADD = common/libcacheismocommon.la \
                  fallocator/libcacheismofallocator.la \
                  cacheitem/libcacheismocacheitem.la \
                  chunkpool/libcacheismochunkpool.la \
                  datastream/libcacheismodatastream.la \
                  hashmap/libcacheismohashmap.la \
                  io/libcacheismoio.la \
                  parser/libcacheismoparser.la \
                  cluster/libcacheismocluster.la \
                  lua/libcacheismolua.la 

cacheismo_SOURCES = cacheismo.c cacheismo.o


