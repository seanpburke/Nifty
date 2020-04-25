################################################################################
#
# Build settings. You will need to adjust these depending on your platform.
#
CC		= cc
CPPFLAGS	= -I../include

# Select debug or optimized build.
#
ifdef NDEBUG
CFLAGS		= -std=gnu11 -Wall -Wno-unused -DNDEBUG -O3
else
CFLAGS		= -std=gnu11 -Wall -g
endif

ifdef NFT_LOCKLESS
CPPFLAGS	+= -DNFT_LOCKLESS -DNFT_HMAPSZINI=16 -DNFT_HMAPSZMAX=16
endif

# By default, use gettimeofday() to get accurate time.
# See ../include/nft_gettime.h for other choices.
#
CPPFLAGS	+= -DUSE_GETTIMEOFDAY

# Choose appropriate link libraries.
#
LDLIBS		= -lpthread			# Solaris, Linux, AIX
#LDLIBS		=				# MacOSX, MinGW

# Uncomment to use valgrind in the test programs.
#VALGRIND	= valgrind --leak-check=full

################################################################################
#
# You should not need to alter the definitions below.
#
LIBDIR	= ../lib
LIB	= $(LIBDIR)/libnifty.a
SRCS	= nft_core.c nft_handle.c nft_pool.c nft_queue.c nft_rbtree.c nft_string.c nft_task.c nft_win32.c
OBJS	= $(SRCS:.c=.o)
EXES	= $(SRCS:.c=)


all:	$(LIB)

# Execute the unit tests.
test:	$(EXES)
	$(VALGRIND) ./nft_handle
	$(VALGRIND) ./nft_core
	$(VALGRIND) ./nft_queue  < /usr/share/dict/words
	$(VALGRIND) ./nft_pool
	$(VALGRIND) ./nft_rbtree < /usr/share/dict/words
	$(VALGRIND) ./nft_string
	$(VALGRIND) ./nft_task
	$(VALGRIND) ./nft_win32

$(LIB): $(LIBDIR) $(OBJS)
	ar -ruv $@ $(OBJS)

.c.o:
	$(CC) -c $(CPPFLAGS) $(CFLAGS) $<

#
# Build the unit test programs
#
nft_core: nft_core.c ../include/nft_core.h $(LIB)
	$(CC) $(CPPFLAGS) $(CFLAGS) -DMAIN $@.c $(LIB) $(LDLIBS) -o $@

nft_handle: nft_handle.c ../include/nft_handle.h $(LIB)
	$(CC) $(CPPFLAGS) $(CFLAGS) -DMAIN $@.c $(LIB) $(LDLIBS) -o $@

nft_pool: nft_pool.c ../include/nft_pool.h $(LIB)
	$(CC) $(CPPFLAGS) $(CFLAGS) -DMAIN $@.c $(LIB) $(LDLIBS) -o $@

nft_queue: nft_queue.c ../include/nft_queue.h $(LIB)
	$(CC) $(CPPFLAGS) $(CFLAGS) -DMAIN $@.c $(LIB) $(LDLIBS) -o $@

nft_rbtree: nft_rbtree.c ../include/nft_rbtree.h $(LIB)
	$(CC) $(CPPFLAGS) $(CFLAGS) -DMAIN $@.c $(LIB) $(LDLIBS) -o $@

nft_string: nft_string.c ../include/nft_string.h $(LIB)
	$(CC) $(CPPFLAGS) $(CFLAGS) -DMAIN $@.c $(LIB) $(LDLIBS) -o $@

nft_task: nft_task.c ../include/nft_task.h $(LIB)
	$(CC) $(CPPFLAGS) $(CFLAGS) -DMAIN $@.c $(LIB) $(LDLIBS) -o $@

nft_win32: nft_win32.c ../include/nft_win32.h $(LIB)
	$(CC) $(CPPFLAGS) $(CFLAGS) -DMAIN $@.c $(LIB) $(LDLIBS) -o $@

$(LIBDIR):
	mkdir -p $(LIBDIR)

clean:
	-rm *.o *.exe $(LIB) $(EXES)
	-rm -r *.dSYM
