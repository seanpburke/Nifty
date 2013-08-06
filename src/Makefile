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
CFLAGS		= -Wall -g  
#CFLAGS		= -Wall -O2 -DNDEBUG		# optimized

#
# Choose appropriate link libraries.
#
LDLIBS		= -lpthread			# Solaris, Linux, AIX
#LDLIBS		= -lkse				# FreeBSD 5


################################################################################
#
# You should not need to alter the definitions below.
#
LIB		= ../lib/libnifty.a
SRCS		= nft_core.c
OBJS		= ${SRCS:.c=.o}
EXES		= ${SRCS:.c=}
DIST		= ../Nifty.tgz

all:	$(LIB)

test:	$(EXES)
	./nft_core

dist:    $(DIST)


$(LIB): $(OBJS)
	ar -ruv $@ $(OBJS)

.c.o:
	$(CC) -c $(CPPFLAGS) $(CFLAGS) $<

#
# Build the unit test programs
#
nft_core: nft_core.c				
	$(CC) $(CPPFLAGS) $(CFLAGS) -DMAIN $@.c $(LDLIBS) -o $@


$(DIST): clean
	tar zcvf $@ -C ../.. --exclude '.git' --exclude '*~' --exclude '*.tgz' Nifty

clean:
	- rm *.o $(LIB) $(EXES) $(DIST)
