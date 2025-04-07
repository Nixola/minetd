CC=gcc
CFLAGS=-Wall -Wextra -Wpedantic --std=c99

default: minetd

minetd: minetd.c
	$(CC) $(CFLAGS) minetd.c -o minetd

clean:
	-rm minetd

test:
	nc -l -p 25565 | hexdump -C
