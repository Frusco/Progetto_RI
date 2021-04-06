#include "my_sockets.h"

int open_tcp_server_socket(int *socket_id,struct sockaddr_in *socket_des,int port){
     *socket_id = socket(AF_INET,SOCK_STREAM,0);
     memset(socket_des,0,sizeof(*socket_des)); //Pulizia
     socket_des->sin_family = AF_INET; //Tipo di socket
     socket_des->sin_port = htons(port);//Porta
     socket_des->sin_addr.s_addr = INADDR_ANY;
    return bind(*socket_id,(struct sockaddr*)socket_des,sizeof(*socket_des));
}

int open_tcp_client_socket(int *socket_id,struct sockaddr_in *socket_des,int port){
     *socket_id = socket(AF_INET,SOCK_STREAM,0);
     memset(socket_des,0,sizeof(*socket_des)); //Pulizia
     socket_des->sin_family = AF_INET; //Tipo di socket
     socket_des->sin_port = htons(port);//Porta
     inet_pton(AF_INET,LOCAL_HOST,&socket_des->sin_addr);//Indirizzo da presentation a number
    return connect(*socket_id,(struct sockaddr*)socket_des,sizeof(*socket_des));
}

int check_port(long port_long){
    return (port_long > MIN_PORT && port_long < MAX_PORT);
}

int open_udp_socket(int *socket_id,struct sockaddr_in *socket_des,int port){
    *socket_id = socket(AF_INET,SOCK_DGRAM,0);
     memset(socket_des,0,sizeof(*socket_des)); //Pulizia
     socket_des->sin_family = AF_INET; //Tipo di socket
     socket_des->sin_port = htons(port);//Porta
     socket_des->sin_addr.s_addr = INADDR_ANY;
    return bind(*socket_id,(struct sockaddr*)socket_des,sizeof(*socket_des));
}