CC=cc
CFLAGS=-I.

pebs_monitor: pebs_monitor.c
	$(CC) -o pebs_monitor pebs_monitor.c $(CFLAGS)
