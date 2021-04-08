all: ds peer

peer: peer.o my_parser.o my_sockets.o
	gcc -Wall peer.o my_parser.o my_sockets.o -o peer

ds: ds.o my_parser.o my_sockets.o
	gcc -Wall ds.o my_parser.o my_sockets.o -o ds

peer.o: consts/const.h libs/my_parser.h libs/my_sockets.h Peer/peer.c
	gcc -Wall Peer/peer.c

ds.o: consts/const.h libs/my_parser.h libs/my_sockets.h Discovery_Server/ds.c
	gcc -Wall Discovery_Server/ds.c

my_parser.o : consts/const.h
	gcc -Wall my_parser.c

my_sockets.o: consts/const.h
	gcc -Wall my_sockets.c

clean:
	rm *o peer server