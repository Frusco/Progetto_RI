#include "../consts/const.h"
#include "../libs/my_sockets.h"
#include <stdio.h>
#include <stdlib.h>
#include <arpa/inet.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>

#define DEFAULT_TABLE_SIZE 2
#define DEFAULT_NEIGHBOUR_VECTOR_SIZE 2

/*
Elemento che definisce un peer in una lista ordinata
int id è l'indice della peers_table
int port è il valore che definisce l'ordinamento
*/
struct peer_elem{
    int id;
    int port;
    struct peer_elem* next;
    struct peer_elem* prev;
};
struct peer_elem* peer_list_head;
struct peer_elem* peer_list_tail;

/*
Descrittore di un peer
*/
struct peer_des{
    struct sockaddr_in addr;
    int port;
    int neighbours_number; //Numero dei vicini
    int  neighbours_vector_size;
    int* neighbours_vector; //Array dinamico degli id dei vicini
};
int peers_number; //Il numero dei peer attualmente nelle rete
int table_size; //La grandezza della peers_table
struct peer_des* peers_table; //Tabella dei descrittori di peer

//Restituisce un puntatore al descritore di peer con l'id passato ( se esiste )
struct peer_des* get_peer_des(int id){
    if(id >= table_size) return -1; // Indice troppo altro rispetto alla grandezza della tabella
    if(peers_table[id].port == -1) return -1; // elemento vuoto della peers_table
    return &peers_table[id];
}


// Stampa le informazioni del peer con l'id passa ( se esiste )
void print_peer(int id){
    struct peer_des* p = get_peer_des(id);
    if(p){
        printf("ID: %d\nport:%d\nneighbours_number:%d\nneighbours_list: ",id,p->port,p->neighbours_number);
        for(int i = 0 ; i<p->neighbours_number;i++){
            printf("%d ",p->neighbours_vector[i]);
        }
        printf("\n");
    }
}

void print_all_peers(){
    for(int i = 0 ; i<table_size; i++){
        print_peer(i);
    }
}

/*
### GLOBALS INIT E FREE ################################################
*/
void globals_init(){
    peer_list_head = NULL;
    peer_list_tail = NULL;
    peers_number = 0;
    table_size = DEFAULT_TABLE_SIZE;
    peers_table = malloc(sizeof(struct peer_des)*table_size);
    for(int i = 0 ; i<table_size; i++){
// Gli elementi vuoti della peers_table sono identificati con porta = -1
        peers_table[i].port = -1;
    }
}

void free_peers_table(){
    for(int i = 0 ; i<table_size; i++){
        if(peers_table[i].port == -1) continue;
        free(peers_table[i].neighbours_vector);
    }
    free(peers_table);
}

void free_peers_list(){
    struct peer_elem* aux;
    while(peer_list_head!=NULL){
        aux = peer_list_head;
        peer_list_head = peer_list_head->next;
        free(aux);
    }
    peer_list_tail = NULL;
}


free_globals(){
    free_peers_list();
    free_globals();
}

/*
### GESTIONE PEERS_TABLE ##############################################
*/

/*  
Elementi della struct peer_des
    struct sockaddr_in addr;
    int port;
    int neighbours_number; //Numero dei vicini
    int* neighbours_vector; //Array dinamico degli id dei vicini
*/

void init_peer_table_row(int i,struct sockaddr_in addr, int port){
    struct peer_des *pd = &peers_table[i];
    pd->addr = addr;
    pd->port = port;
    pd->neighbours_number = 0;
    pd->neighbours_vector_size = DEFAULT_NEIGHBOUR_VECTOR_SIZE;
    pd->neighbours_vector = malloc(sizeof(int)*pd->neighbours_vector_size);
    peers_number++;
}

/* 
Restituisce l'id del peer (indice della tabella dei descrittori di peer) 
dopo aver allocato il descrittore del nuovo peer
*/
int peers_table_add_peer(struct sockaddr_in addr, int port){
    int i;
    for(i = 0 ; i<table_size;i++){
        if(peers_table[i].port==-1){// trovata riga libera
            populate_peer_table_row(i,addr,port);
            return i;
        }
    }
    /*
Se siamo giunti fino a qui vuol dire che l'array è pieno
Raddoppiamo la sua grandezza moltiplicando per due table_size
    */
   table_size = table_size*2;
   realloc(peers_table,sizeof(struct peer_des)*table_size);
   for(int j = i+1 ; j<table_size;j++){
       peers_table[j].port = -1;
   }
   populate_peer_table_row(i,addr,port);
   return i;
}

void peers_table_remove_peer(int i){
    struct peer_des *pd;
    if(i>=table_size) return;
    pd = &peers_table[i];
    if(pd->port == -1)return;
    pd->port = -1;
    free(pd->neighbours_vector);
    peers_number--;
}

/*
Aggiunge un vicino al vettore dinamico dei vicini ( neighbour_vector )
*/
int peers_table_add_neighbour(int id , int neighbour_id){
    int i;
    struct peer_des *pd;
    if(id>=table_size) return -1;
    pd = &peers_table[id];
    if(pd->port == -1)return -1;
    for(i = 0 ; i<pd->neighbours_vector_size;i++){
        if(pd->neighbours_vector[i]==-1){// trovata riga libera
            pd->neighbours_vector[i] = neighbour_id;
            pd->neighbours_number++;
            return 1;
        }
    }
    /*
Se siamo giunti fino a qui vuol dire che l'array è pieno
Raddoppiamo la sua grandezza moltiplicando per due neighbours_vector_size
    */
   pd->neighbours_vector_size = pd->neighbours_vector_size*2;
   realloc(pd->neighbours_vector,sizeof(int)*pd->neighbours_vector_size);
   for(int j = i+1 ; j<pd->neighbours_vector_size;j++){
       pd->neighbours_vector[j] = -1;
   }
   pd->neighbours_vector[i] = neighbour_id;
   pd->neighbours_number++;
   return 1;
}

void peers_table_remove_neighbour(int id, int neighbour_id){
    struct peer_des *pd;
    if(id>=table_size) return;
    pd = &peers_table[id];
    if(pd->port == -1) return;
    for(int i = 0 ; i<pd->neighbours_vector_size;i++){
        if(pd->neighbours_vector[i] == neighbour_id){
            pd->neighbours_vector[1] = -1;
            pd->neighbours_number--;
            return;
        }
    }
}



/*######################################################################################### REFERENCE*/
int main(int argc, char* argv[]){
    int s_id,c_id,len;
    char buffer[1024];
    struct sockaddr_in s_addr;
    struct sockaddr_in c_addr;
    int port = DEFAULT_PORT;
    int ret,max_id;
    fd_set master;
    fd_set to_read;
    FD_ZERO(&master);
    FD_ZERO(&to_read);
    if(argc>1){
        long pl = strtol(argv[1],0,10);
        if(check_port(pl)){
            port = pl;
        }
    }
    ret = open_tcp_server_socket(&s_id,&s_addr,port);
    if(ret<0){
        perror("Errore nella costruzione della server socket\n");
        exit(-1);
    }
    ret = listen(s_id,DEFAULT_SOCKET_SLOTS);
    printf("Server aperto alla porta: %d\n",port);
    FD_SET(s_id,&master);
    printf("Server socket id = %d\n",s_id);
    max_id = s_id;
    
    while(1){
        to_read = master;
        printf("In attesa di una richiesta...\n");
        select(max_id+1,&to_read,NULL,NULL,NULL);
        for(int socket_i = 0; socket_i<=max_id;socket_i++){
            if(FD_ISSET(socket_i,&to_read)){//Socket ready to read;
                if(socket_i == s_id){
                    len = sizeof(c_addr);
                    c_id = accept(s_id,(struct sockaddr*) &c_addr,&len);
                    printf("È arrivato un nuovo client! : %d\n",c_id);
                    FD_SET(c_id,&master);
                    if(c_id>max_id) {max_id = c_id;}
                }else{//Serviamo un client;
                    len = 1024;
                    printf("Servo il client : %d\n",socket_i);
                    if(recv(socket_i,(void*)buffer,len,0)<0){
                        perror("Errore in fase di ricezione\n");
                        continue;
                    }
                    printf("Ricevuto < %s > ora rinvio \n",buffer);
                    if(strcmp("ciaone",buffer)==0){
                        printf("Ricevuto comando di uscita\n");
                        close(socket_i);
                        FD_CLR(socket_i,&master);
                        continue;
                    }
                    if(send(socket_i,(void*)buffer,len,0)<0){
                        perror("Errore in fase di invio\n");
                    }
                    

                }
            }
        }\
        
    }

}