CC=c++
CFLAGS=-I. -std=c++20

PerfEvent: PerfEvent.c
	$(CC) -o PerfEvent PerfEvent.c $(CFLAGS)
