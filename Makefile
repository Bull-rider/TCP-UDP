CC=arm-linux-gcc
LD=arm-linux-ld

CFLAGS=-g -I. -Wall -O0
LDFLAGS=-lpthread

all: clean socket_server
	cp socket_server /opt/target

clean: 
	rm -f socket_server *.o

socket_server: socket_server.o parse_stream.o gen_helper.o thread_pool.o

socket_server-static: socket_server.o parse_stream.o gen_helper.o thread_pool.o
	$(CC) -static -o $@ $?
