################################################################################
#
# Build settings. You will need to adjust these depending on your platform.
#
CC		= gcc
CPPFLAGS	= -I../include

# By default, use gettimeofday() to get accurate time.
# See ../include/nft_gettime.h for other choices.
#
CPPFLAGS	+= -DUSE_GETTIMEOFDAY

# CPPFLAGS	+= -D_ALL_SOURCE		# Uncomment for Windows SFU builds.

# Select debug or optimized build.
#
CFLAGS		= -std=gnu99 -Wall -Wno-unused-function -g
#CFLAGS		= -std=gnu99 -Wall -Wno-unused-function -O2 -DNDEBUG

# Choose appropriate link libraries.
#
#LDLIBS		= -lpthread			# Solaris, Linux, AIX
#LDLIBS		= -lkse				# FreeBSD 5
LDLIBS		=				# MacOSX, MinGW

# Uncomment to use valgrind in the test programs.
#VALGRIND	= valgrind --leak-check=full

################################################################################
#
# You should not need to alter the definitions below.
#
VERSION = 0.92
LIBDIR	= ../lib
LIB	= $(LIBDIR)/libnifty.a
SRCS	= nft_core.c nft_hash.c nft_pool.c nft_queue.c nft_task.c nft_win32.c
OBJS	= $(SRCS:.c=.o)
EXES	= $(SRCS:.c=)
DISTDIR = Nifty-$(VERSION)
DIST	= ../../Nifty-$(VERSION).tgz

all:	$(LIB)

test:	$(EXES)
	$(VALGRIND) ./nft_core
	$(VALGRIND) ./nft_queue  < /usr/share/dict/words
	$(VALGRIND) ./nft_pool
	$(VALGRIND) ./nft_task
	$(VALGRIND) ./nft_hash   < /usr/share/dict/words
	$(VALGRIND) ./nft_win32

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

nft_win32: nft_win32.c ../include/nft_win32.h $(LIB)
	$(CC) $(CPPFLAGS) $(CFLAGS) -DMAIN $@.c $(LIB) $(LDLIBS) -o $@

$(DIST): clean $(DISTDIR)
	tar cvf -                \
	$(DISTDIR)/README.txt    \
	$(DISTDIR)/include/*.h   \
	$(DISTDIR)/src/*.c       \
	$(DISTDIR)/src/Makefile  | gzip -c > $@

$(DISTDIR):
	ln -s ../../Nifty $(DISTDIR)

$(LIBDIR):
	mkdir -p $(LIBDIR)

clean:
	-rm *.o *.exe $(LIB) $(EXES) $(DISTDIR)
