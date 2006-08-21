
CFLAGS := -g -O2 -Wall -DHAVE_EPOLL

watcher: watcher.o fdevent.o
	$(CC) -o watcher watcher.o fdevent.o

clean:
	rm -f *~ *.o watcher
