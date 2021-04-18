all: ds peer

# peer: peer.o my_parser.o my_sockets.o
#	gcc -Wall peer.o my_parser.o my_sockets.o -o peer




ds: ds.o my_parser.o my_sockets.o ds.o
	gcc -Wall ds.o my_parser.o my_sockets.o -lpthread -std=c99 -o ds
peer: peer.o my_sockets.o
	gcc -Wall peer.o my_sockets.o -lpthread -std=c99 -o peer

peer.o: consts/const.h Peer/peer.c
	gcc -Wall -c Peer/peer.c -lpthread -std=c99

ds.o: consts/const.h Discovery_Server/ds.c
	gcc -Wall -c Discovery_Server/ds.c -lpthread -std=c99

my_parser.o : consts/const.h libs/my_parser.h libs/my_parser.c
	gcc -Wall -c libs/my_parser.c

my_sockets.o: consts/const.h libs/my_sockets.h libs/my_sockets.c
	gcc -Wall -c libs/my_sockets.c

clean:
	rm *.o