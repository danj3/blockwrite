LDFLAGS=-L/usr/local/lib -lpopt
CFLAGS=-I/usr/local/include -g -ggdb -Wall

blockwrite: blockwrite.c
	cc -I/usr/local/include -Wall -L/usr/local/lib -o blockwrite blockwrite.c -lpopt

clean:
	rm blockwrite
