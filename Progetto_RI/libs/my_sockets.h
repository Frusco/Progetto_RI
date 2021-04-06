//Costruisce una server socket TCP
#include <stdio.h>
#include <arpa/inet.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>

#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include "../consts/const.h"


int open_tcp_server_socket(int *socket_id,struct sockaddr_in *socket_des,int port);
int open_tcp_client_socket(int *socket_id,struct sockaddr_in *socket_des,int port);
int open_udp_socket(int *socket_id,struct sockaddr_in *socket_des,int port);
int check_port(long port);