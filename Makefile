#VERSION=1.0
#DATE=2017-11-18

CC := gcc

ifeq (1, ${DEBUG})
CFLAGS=-g3 -W -Wall -Wno-unused-but-set-variable -O0 -D_FILE_OFFSET_BITS=64 -D_GNU_SOURCE -mpopcnt -msse4.2
else
CFLAGS=-g3 -W -Wall -Wno-unused-but-set-variable -O4 -D_FILE_OFFSET_BITS=64 -D_GNU_SOURCE -mpopcnt -msse4.2
endif

INSTALLDIR=/usr/local/bin
GLIBS=-lm -lrt -lpthread
GENERIC_SRC=mem_share.h string.h filereader.h bitvec.h hashset.h sort.h list.h thread.h

PROGS=psort

all: $(PROGS)

psort: $(GENERIC_SRC) psort.c
	$(CC) $(CFLAGS) -o psort psort.c $(GLIBS)

clean:
	rm -f *.o *.gcda *.gcno *.gcov gmon.out $(PROGS)

clear:
	rm -f *.o *.gcda *.gcno *.gcov gmon.out

install:
	cp -fvu $(PROGS) $(INSTALLDIR)
