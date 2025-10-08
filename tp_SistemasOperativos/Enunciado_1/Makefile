CC = gcc
CFLAGS = -std=c11 -Wall -Wextra -O2 -D_POSIX_C_SOURCE=200809L
LDFLAGS = -pthread

all: tp

tp: main.o registro.o
	$(CC) $(CFLAGS) -o tp main.o registro.o $(LDFLAGS)

main.o: main.c registro.h
	$(CC) $(CFLAGS) -c main.c

registro.o: registro.c registro.h
	$(CC) $(CFLAGS) -c registro.c

clean:
	rm -f *.o tp registros.csv
.PHONY: all clean

