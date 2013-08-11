################################################################################
#
# Build settings. You will need to adjust these depending on your platform.
#
CC		= gcc
CPPFLAGS	= -I../include -D_REENTRANT

#
# By default, use gettimeofday() to get accurate time.
# See ../include/nft_gettime.h for other choices.
#
CPPFLAGS	+= -DUSE_GETTIMEOFDAY

# CPPFLAGS	+= -D_ALL_SOURCE		# Uncomment for Windows SFU builds.

#
# Select debug or optimized build.
#
CFLAGS		= -std=gnu99 -Wall -g
#CFLAGS		= -std=gnu99 -Wall -O2 -DNDEBUG	# optimized

#
# Choose appropriate link libraries.
#
LDLIBS		= -lpthread			# Solaris, Linux, AIX
#LDLIBS		= -lkse				# FreeBSD 5


################################################################################
#
# You should not need to alter the definitions below.
#
LIBDIR	= ../lib
LIB	= $(LIBDIR)/libnifty.a
SRCS	= nft_core.c nft_hash.c nft_pool.c nft_queue.c nft_task.c
OBJS	= $(SRCS:.c=.o)
EXES	= $(SRCS:.c=)
DIST	= ../Nifty.tgz

all:	$(LIB)

test:	$(EXES)
	./nft_hash   < /usr/share/dict/words	# Use /usr/dict/words on Solaris
	./nft_core
	./nft_queue  < /usr/share/dict/words
	./nft_pool
	./nft_task

dist:    $(DIST)


$(LIB): $(LIBDIR) $(OBJS)
	ar -ruv $@ $(OBJS)

.c.o:
	$(CC) -c $(CPPFLAGS) $(CFLAGS) $<

#
# Build the unit test programs
#
nft_core: nft_core.c ../include/nft_core.h $(LIB)
	$(CC) $(CPPFLAGS) $(CFLAGS) -DMAIN $@.c $(LIB) $(LDLIBS) -o $@

nft_hash: nft_hash.c ../include/nft_hash.h $(LIB)
	$(CC) $(CPPFLAGS) $(CFLAGS) -DMAIN $@.c $(LIB) $(LDLIBS) -o $@

nft_pool: nft_pool.c ../include/nft_pool.h $(LIB)
	$(CC) $(CPPFLAGS) $(CFLAGS) -DMAIN $@.c $(LIB) $(LDLIBS) -o $@

nft_queue: nft_queue.c ../include/nft_queue.h $(LIB)
	$(CC) $(CPPFLAGS) $(CFLAGS) -DMAIN $@.c $(LIB) $(LDLIBS) -o $@

nft_task: nft_task.c ../include/nft_task.h $(LIB)
	$(CC) $(CPPFLAGS) $(CFLAGS) -DMAIN $@.c $(LIB) $(LDLIBS) -o $@

$(DIST): clean
	tar zcvf $@ -C ../.. --exclude '.git' --exclude '*~' --exclude '*.tgz' Nifty

$(LIBDIR):
	mkdir -p $(LIBDIR)

clean:
	- rm *.o $(LIB) $(EXES) $(DIST)
