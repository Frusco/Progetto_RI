all: ds peer

# peer: peer.o my_parser.o my_sockets.o
#	gcc -Wall peer.o my_parser.o my_sockets.o -o peer




ds: ds.o my_sockets.o my_logger.o
	gcc -Wall ds.o my_sockets.o my_logger.o -lpthread -std=c11 -o ds
peer: peer.o my_sockets.o my_logger.o
	gcc -Wall peer.o my_sockets.o my_logger.o -lpthread -std=c11 -o peer

peer.o: consts/peer_consts.h consts/socket_consts.h Peer/peer.c
	gcc -Wall -c Peer/peer.c -lpthread -std=c11

ds.o: consts/ds_consts.h consts/socket_consts.h Discovery_Server/ds.c
	gcc  -Wall -c Discovery_Server/ds.c -lpthread -std=c11

my_logger.o: libs/my_logger.h libs/my_logger.c
	gcc -Wall -c libs/my_logger.c -lpthread -std=c11


my_sockets.o: consts/socket_consts.h libs/my_sockets.h libs/my_sockets.c
	gcc -Wall -c libs/my_sockets.c -std=c11

rm_o:
	rm *.o

clean:
	rm *.o
	rm -r __*
	rm ds_*.log
	rm ds_sync_time.txt