CC      = gcc
CFLAGS  = -Wall -Wextra -g -pthread
AR      = ar
ARFLAGS = rcs
SHM_KEY = 0x5678

.PHONY: all clean shmclean

all: shmclean libksocket.a initksocket user1 user2

shmclean:
	@echo "Cleaning shared memory (if exists)..."
	@ipcrm -M $(SHM_KEY) 2>/dev/null || true

ksocket.o: ksocket.c ksocket.h
	$(CC) $(CFLAGS) -c ksocket.c -o ksocket.o

libksocket.a: ksocket.o
	$(AR) $(ARFLAGS) libksocket.a ksocket.o

initksocket.o: initksocket.c ksocket.h
	$(CC) $(CFLAGS) -c initksocket.c -o initksocket.o

initksocket: initksocket.o ksocket.o
	$(CC) $(CFLAGS) -o initksocket initksocket.o ksocket.o -pthread
user1: user1.c ksocket.h libksocket.a
	$(CC) $(CFLAGS) -o user1 user1.c -L. -lksocket -pthread

user2: user2.c ksocket.h libksocket.a
	$(CC) $(CFLAGS) -o user2 user2.c -L. -lksocket -pthread

clean:
	rm -f *.o libksocket.a initksocket user1 user2
	@echo "Cleaning shared memory..."
	@ipcrm -M $(SHM_KEY) 2>/dev/null || true