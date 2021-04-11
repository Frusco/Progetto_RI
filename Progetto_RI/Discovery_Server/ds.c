#include "../consts/const.h"
#include "../libs/my_sockets.h"
#include "../libs/my_parser.h"
#include <stdio.h>
#include <stdlib.h>
#include <arpa/inet.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <unistd.h>
#include <pthread.h>
//#define MIN_NEIGHBOUR_NUMBER 2
//#define DEFAULT_TABLE_SIZE 2
//#define DEFAULT_NEIGHBOUR_VECTOR_SIZE 2
/*
DISCOVERY SERVER:


*/

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
struct peer_elem* peers_list;
struct peer_elem* peers_list_tail;
pthread_mutex_t list_mutex;

/*
Descrittore di un peer
*/
struct peer_des{
    struct in_addr addr;
    int port;
    int neighbors_number; //Numero dei vicini
    int  neighbors_vector_size;
    int* neighbors_vector; //Array dinamico degli id dei vicini
};
int peers_number; //Il numero dei peer attualmente nelle rete
int peers_table_size; //La grandezza della peers_table
struct peer_des* peers_table; //Tabella dei descrittori di peer
pthread_mutex_t table_mutex;

//Restituisce un puntatore al descritore di peer con l'id passato ( se esiste )
struct peer_des* get_peer_des(int id){
    if(id >= peers_table_size) return NULL; // Indice troppo altro rispetto alla grandezza della tabella
    if(peers_table[id].port == -1) return NULL; // elemento vuoto della peers_table
    return &peers_table[id];
}


// Stampa le informazioni del peer con l'id passa ( se esiste )
void peers_table_print_peer(int id){
    struct peer_des* p = get_peer_des(id);
    if(p){
        printf("\nID: %d\naddr: %u\nport: %d\nneighbors_number: %d\nneighbors_list: ",id,p->addr.s_addr,p->port,p->neighbors_number);
        for(int i = 0 ; i<p->neighbors_vector_size;i++){
            if(p->neighbors_vector[i]==-1)continue;
            printf("%d ",p->neighbors_vector[i]);
        }
        printf("\n");
    }
}

void peers_table_print_peer_neighbor(int id){
    pthread_mutex_lock(&table_mutex);
    struct peer_des* p = get_peer_des(id);
    if(p){
        printf("ID: %d\nneighbors_number: %d\nneighbors_list: ",id,p->neighbors_number);
        for(int i = 0 ; i<p->neighbors_vector_size;i++){
            if(p->neighbors_vector[i]==-1)continue;
            printf("%d ",p->neighbors_vector[i]);
        }
        printf("\n");
    }
    pthread_mutex_unlock(&table_mutex);
}

void peers_table_print_all_peers(){
    pthread_mutex_lock(&table_mutex);
    for(int i = 0 ; i<peers_table_size; i++){
        peers_table_print_peer(i);
    }
    pthread_mutex_unlock(&table_mutex);
}

/*
### GLOBALS INIT E FREE ################################################
*/
int loop_flag;
void globals_init(){
    peers_list = NULL;
    peers_list_tail = NULL;
    loop_flag=1;
    peers_number = 0;
    peers_table_size = DEFAULT_TABLE_SIZE;
    peers_table = malloc(sizeof(struct peer_des)*peers_table_size);
    for(int i = 0 ; i<peers_table_size; i++){
// Gli elementi vuoti della peers_table sono identificati con porta = -1
        peers_table[i].port = -1;
    }
    pthread_mutex_init(&list_mutex,NULL);
    pthread_mutex_init(&table_mutex,NULL);
}

void free_peers_table(){
    for(int i = 0 ; i<peers_table_size; i++){
        if(peers_table[i].port == -1) continue;
        free(peers_table[i].neighbors_vector);
    }
    free(peers_table);
}

void free_peers_list(){
    struct peer_elem* aux;
    while(peers_list!=NULL){
        aux = peers_list;
        peers_list = peers_list->next;
        free(aux);
    }
    peers_list_tail = NULL;
}


void globals_free(){
    free_peers_list();
    free_peers_table();
}

/*
### GESTIONE PEERS_TABLE ##############################################
*/

/*  
Elementi della struct peer_des
    struct sockaddr_in addr;
    int port;
    int neighbors_number; //Numero dei vicini
    int  neighbors_vector_size;
    int* neighbors_vector; //Array dinamico degli id dei vicini
*/

void populate_peers_table_row(int i,struct in_addr addr, int port){
    struct peer_des *pd = &peers_table[i];
    pd->addr = addr;
    pd->port = port;
    pd->neighbors_number = 0;
    pd->neighbors_vector_size = DEFAULT_NEIGHBOUR_VECTOR_SIZE;
    pd->neighbors_vector = malloc(sizeof(int)*pd->neighbors_vector_size);
    for(int i = 0 ; i< pd->neighbors_vector_size; i++){
        pd->neighbors_vector[i] = -1;// inizializzo come vuoti
    }
    peers_number++;
}

/* 
Restituisce l'id del peer (indice della tabella dei descrittori di peer) 
dopo aver allocato il descrittore del nuovo peer
*/
int peers_table_add_peer(struct in_addr addr, int port){
    int i;
    int backup_peers_table_size;
    void * aux;
    for(i = 0 ; i<peers_table_size;i++){
        if(peers_table[i].port==-1){// trovata riga libera
            populate_peers_table_row(i,addr,port);
            return i;
        }
    }
    /*
Se siamo giunti fino a qui vuol dire che l'array è pieno
Raddoppiamo la sua grandezza moltiplicando per due peers_table_size e usando realloc
    */
   backup_peers_table_size = peers_table_size;
   peers_table_size = peers_table_size*2;
   //printf("Memoria richiesta %ld byte\n",sizeof(struct peer_des)*peers_table_size);
   aux = realloc(peers_table,sizeof(struct peer_des)*peers_table_size);
   //printf("AUX %p\n",aux);
   if(aux == NULL){
       perror("Memoria insufficiente");
       peers_table_size = backup_peers_table_size;
       return -1;
   }
   peers_table = (struct peer_des*)aux;
   for(int j = i+1 ; j<peers_table_size;j++){
       peers_table[j].port = -1;
   }
   populate_peers_table_row(i,addr,port);
   return i;
}

void peers_table_remove_peer(int i){
    struct peer_des *pd;
    if(i>=peers_table_size) return;
    pd = &peers_table[i];
    if(pd->port == -1)return;
    pd->port = -1;
    free(pd->neighbors_vector);
    peers_number--;
}

/*
Aggiunge un vicino al vettore dinamico dei vicini ( neighbour_vector )
*/
int peers_table_add_neighbour(int id , int neighbour_id){
    //printf("Sono %d e aggiungo %d alla mia lista\n",id,neighbour_id);
    int i;
    int backup_neighbors_size;
    void* aux;
    struct peer_des *pd;
    if(id>=peers_table_size) return -1;
    pd = &peers_table[id];
    if(pd->port == -1)return -1;
    for(i = 0 ; i<pd->neighbors_vector_size;i++){
        if(pd->neighbors_vector[i]==-1){// trovata riga libera
            pd->neighbors_vector[i] = neighbour_id;
            pd->neighbors_number++;
            return 1;
        }
    }
    /*
Se siamo giunti fino a qui vuol dire che l'array è pieno
Raddoppiamo la sua grandezza moltiplicando per due neighbors_vector_size
    */
   backup_neighbors_size = pd->neighbors_vector_size;
   pd->neighbors_vector_size = pd->neighbors_vector_size*2;
   //printf("Memoria richiesta %ld byte\n",sizeof(int)*pd->neighbors_vector_size);
   aux = realloc(pd->neighbors_vector,sizeof(int)*pd->neighbors_vector_size);
   //printf("AUX %p\n",aux);
   if(aux == NULL){
       perror("Memoria insufficiente");
       pd->neighbors_vector_size = backup_neighbors_size;
       return -1;
   }
   pd->neighbors_vector = (int*)aux;
   for(int j = i+1 ; j<pd->neighbors_vector_size;j++){
       pd->neighbors_vector[j] = -1;
   }
   pd->neighbors_vector[i] = neighbour_id;
   pd->neighbors_number++;
   return 1;
}

void peers_table_remove_neighbour(int id, int neighbour_id){
    struct peer_des *pd;
    if(id>=peers_table_size) return;
    pd = &peers_table[id];
    if(pd->port == -1) return;
    for(int i = 0 ; i<pd->neighbors_vector_size;i++){
        if(pd->neighbors_vector[i] == neighbour_id){
            pd->neighbors_vector[i] = -1;
            pd->neighbors_number--;
            return;
        }
    }

    
}

// Controlla se il peer id ha come vicino neighbour_id
int peers_table_has_neighbour(int id, int neighbour_id){
// Se id è uguale a neighbour_id restituisce vero
    if(id == neighbour_id) return 1;
    struct peer_des *pd;
    if(id>=peers_table_size) return 0;
    pd = &peers_table[id];
    if(pd->port == -1) return 0;
    for(int i = 0 ; i<pd->neighbors_vector_size;i++){
        if(pd->neighbors_vector[i] == neighbour_id){
            return 1;
        }
    }
    return 0;
}

int get_id_by_ip_port(struct in_addr addr,int port){
    struct peer_des* pd;
    int ret =-1;
    pthread_mutex_lock(&table_mutex);
    for(int i=0 ; i<peers_table_size;i++){
        pd = &peers_table[i];
        if(pd->port==-1) continue;
        if((pd->addr.s_addr == addr.s_addr )&& (pd->port == port)){
            ret = i;
            break;
        }
    }
    pthread_mutex_unlock(&table_mutex);
    return ret;
}

/*
### GESTIONE peers_list ##############################################
*/

/*
Inserimento in una lista ordinata usando due puntatori
*/
struct peer_elem* peer_elem_init(int id,int port){
    struct peer_elem* pe = malloc(sizeof(struct peer_elem));
    pe->id = id;
    pe->port = port;
    pe->next = NULL;
    pe->prev = NULL;
    return pe;
}

void peers_list_add(struct peer_elem* new_peer){
    struct peer_elem *peer, *prev_peer;
    peer = peers_list;
    prev_peer = NULL;
    while(peer != NULL && peer->port <= new_peer->port){
        prev_peer = peer;
        peer = peer->next;
    }
    new_peer->next = peer;
    if(prev_peer == NULL){//new_peer è il primo peer inserito
        peers_list = new_peer;
        peers_list_tail = new_peer;
    }else{
        prev_peer->next = new_peer;
        new_peer->prev = prev_peer;
        peers_table_add_neighbour(prev_peer->id,new_peer->id);
        peers_table_add_neighbour(new_peer->id,prev_peer->id);
        if(peer!=NULL){//Se new_peer non è la coda
            peer->prev = new_peer;
            peers_table_add_neighbour(peer->id,new_peer->id);
            peers_table_add_neighbour(new_peer->id,peer->id);
        }else{//Altrimenti aggiorno il puntatore alla coda
            peers_list_tail = new_peer;
        }
    }
    
}

void peers_list_remove(int id){
    struct peer_elem *peer, *prev_peer;
    peer = peers_list;
    prev_peer = NULL;
    while(peer != NULL && peer->id != id){
        prev_peer = peer;
        peer = peer->next;
    }
    if(prev_peer!=NULL){
        prev_peer->next = peer->next;
    }else{
        peers_list = peer->next;
    }
    if(peer->next!=NULL){
        peer->next->prev = prev_peer;
    }else{
        peers_list_tail = prev_peer;
    }
    if(prev_peer!=NULL && peer->next!=NULL){
// Aggiorno la lista dei vicini
// Il precedente e il successivo peer del peer eliminato potrebbero
// Diventare nuovi vicini (se non lo sono già)
    if(!peers_table_has_neighbour(prev_peer->id,peer->next->id)){
        peers_table_add_neighbour(prev_peer->id,peer->next->id);
    }
    if(!peers_table_has_neighbour(peer->next->id,prev_peer->id)){
        peers_table_add_neighbour(peer->next->id,prev_peer->id);
    }
    }
}

/*
Se il peer in testa alla lista ha meno di due vicini,
scorro la lista in cerca del primo peer non nella sua lista dei vicini,
essendo la lista ordinata sarà anche il più vicino al livello di porta
*/
void print_peer_elem(struct peer_elem* pe);
void fix_head_isolation(){
    if(peers_number<2) return;
    if(peers_list == NULL) return;
    struct peer_elem *peer = peers_list->next;
    int id = peers_list->id;
    struct peer_des *pd = get_peer_des(id);
    //Se possiede la quantità minima di vicini salto il controllo
    if(pd->neighbors_number>=MIN_NEIGHBOUR_NUMBER)return;
    while(peer!=NULL){
        //printf("Sono %d e forse ho trovato un nuovo amichetto %d\n",id,peer->id);
        if(!peers_table_has_neighbour(id,peer->id)){
            //printf("Sono %d e ho trovato un nuovo amichetto %d\n",id,peer->id);
            peers_table_add_neighbour(id,peer->id);
            peers_table_add_neighbour(peer->id,id);
            if(pd->neighbors_number>=MIN_NEIGHBOUR_NUMBER) return;
        }
        peer = peer->next;
    }
}
/*
Se il peer in coda alla lista ha meno di due vicini,
scorro la lista ( al contrario ) in cerca del primo peer non nella sua lista dei vicini,
essendo la lista ordinata sarà anche il più vicino al livello di porta
*/
void fix_tail_isolation(){
    if(peers_number<2) return;
    if(peers_list_tail == NULL) return;
    struct peer_elem *peer = peers_list_tail->prev;
    int id = peers_list_tail->id;
    struct peer_des *pd = get_peer_des(id);
    //Se possiede la quantità minima di vicini salto il controllo
    if(pd->neighbors_number>=MIN_NEIGHBOUR_NUMBER)return;
    while(peer!=NULL){
        if(!peers_table_has_neighbour(id,peer->id)){
            peers_table_add_neighbour(id,peer->id);
            peers_table_add_neighbour(peer->id,id);
            if(pd->neighbors_number>=MIN_NEIGHBOUR_NUMBER) return;
        }
        peer = peer->prev;
    }
}


void print_peer_elem(struct peer_elem* pe){
    printf("[ ID: %d , port: %d ]\n",pe->id,pe->port);
}
void peers_list_print(){
    struct peer_elem *peer;
    peer = peers_list;
    pthread_mutex_lock(&list_mutex);
    printf("Peers list:\n");
    while(peer != NULL){
        print_peer_elem(peer);
        peer = peer->next;
    }
    pthread_mutex_unlock(&list_mutex);
}




/*
### FUNZIONI THREAD ##############################################
*/

/*
Aggiunge una nuova riga alla tabbela dei dei descrittori di peer (peers_table)
restituendo l'indice della tabella (id:int), viene allocato un peer_elem contenente
l'indice della peer_table e la porta utilizzata per ordinare la lista (peers_list) nella
quale sarà inserito.
Effettuato l'inserimento verranno inidividuati i vicini e risolti gli eventuali
problemi di isolamento della coda e della testa della lista.
*/
int add_peer(struct in_addr addr,int port){
    int id;
    struct peer_elem *pe;

    pthread_mutex_lock(&table_mutex);
    id = peers_table_add_peer(addr,port);
    pthread_mutex_unlock(&table_mutex);
    pe = peer_elem_init(id,port);

    pthread_mutex_lock(&list_mutex);
    peers_list_add(pe);
    fix_head_isolation();
    fix_tail_isolation();
    pthread_mutex_unlock(&list_mutex);
    return id;
}
/*
Svuota la riga della peers_table identificata con l'id passato, elimina l'id
dalla lista dei vicini (neighbors_vector) e elimina il relativo peer_elem dalla
lista dei peer, ricalcolando i vicini e risolvendo eventuali problemi di 
isolamento dei peer in testa e coda della lista.
*/
void remove_peer(int id){
    pthread_mutex_lock(&table_mutex);
    peers_table_remove_peer(id);
    for(int i = 0 ; i<peers_table_size;i++){
        if(id == i)continue;
        peers_table_remove_neighbour(i,id);
    }
    pthread_mutex_unlock(&table_mutex);
    pthread_mutex_lock(&list_mutex);
    peers_list_remove(id);
    fix_head_isolation();
    fix_tail_isolation();
    pthread_mutex_unlock(&list_mutex);
}

/*
Genera la lista dei vicini di id e restituisce la dimensione della nuova stringa msg allocata
Formato messaggio:

<id richiedente>,<numero di vicini>
<id>,<addr>,<porta>
...
<id>,<addr>,<porta>

*/
uint16_t generate_neighbors_list_message(int id,char* msg){
    char aux[250]="";
    pthread_mutex_lock(&table_mutex);
    struct peer_des* pd = get_peer_des(id);
    if(pd){
        struct peer_des* neighbour;
        sprintf(aux+strlen(aux),"%d,%d\n",id,pd->neighbors_number);
        for(int i = 0 ; i<pd->neighbors_vector_size;i++){
            if(pd->neighbors_vector[i]==-1)continue;
            neighbour = get_peer_des(pd->neighbors_vector[i]);
            sprintf(aux+strlen(aux),"%d,%u,%d\n",pd->neighbors_vector[i],neighbour->addr.s_addr,neighbour->port);
        }
        pthread_mutex_unlock(&table_mutex);
        msg = malloc(sizeof(char)*(strlen(aux)+1));
        
        strcpy(msg,aux);
        return sizeof(aux)+1;
    }else{
        pthread_mutex_unlock(&table_mutex);
        return 0;
    }
}

/*
Il compito del thread è quello di aspettare richieste dai peer
e soddisfarle gestendo la peers_list e la peers_table
Cosa può fare:
- Aggiungere un peer
- Rimuovere un peer
- Inviare la lista dei vicini a un peer

NOTA: è compito del peer contattare il Discovery Server per ottenere
la lista aggiornata della sua lista dei peer.

*/

/*
Formato del messaggio di richiesta da parte del peer
<ip:in_addr>,<porta:int>

C'è da fare il conto di ID_BUFFER, ora non ho voglia 
*/
void test();
void* thread_loop(void* arg){
    //buffers
    char buffer[DS_BUFFER];
    char *msg=0;
    int msg_len;
    //sockets info
    int ds_socket,peer_addrlen;
    struct sockaddr_in ds_addr,peer_addr;
    int port = 25565;
    //peer info
    struct in_addr peer_in_addr;
    int peer_port=-1;
    int id;
    if(open_udp_socket(&ds_socket,&ds_addr,port)){
        perror("Impossibile aprire socket");
        loop_flag = 0;
        pthread_exit(NULL);
    }
    peer_addrlen = sizeof(peer_addr);
    while(loop_flag){
        if(recvfrom(ds_socket,buffer,DS_BUFFER,0,(struct sockaddr*)&peer_addr,(int*)&peer_addrlen)<0){
            continue;
        }
        scanf(buffer,"%u,%d",peer_addr.sin_addr,peer_port);
        id = get_id_by_ip_port(peer_in_addr,peer_port);
        if(id==-1){//nuovo utente
            id = add_peer(peer_in_addr,peer_port);
        }
        msg_len = generate_neighbors_list_message(id,msg);
        sendto(ds_socket,msg,msg_len,0,(struct sockaddr*)&peer_addr,(int*)&peer_addrlen);
    }
    pthread_exit(NULL);
}

/*######################################################################################### REFERENCE*/
void test(){
    int port = 2000;
    struct in_addr addr;
    inet_pton(AF_INET,"192.168.1.9",&addr);
    for(int i = 0 ; i <10 ; i++){
        add_peer(addr,port+i);
    }
    sleep(1);
    for(int i = 0 ; i <10 ; i++){
         remove_peer(i);
    }
}



int find_command(char* command){
    char command_list[][SERVER_MAX_COMMAND_SIZE] = SERVER_COMMAND_LIST;
    for(int i = 0; i<SERVER_COMMANDS_NUMBER; i++){
        if(strcmp(command,command_list[i]) == 0 ) return i;
    }
    return -1;
}



void user_loop(){
    char msg[40];
    int id;
    int args_number;
    int command_index;
    char args[2][13];
    printf(SERVER_WELCOME_MSG);
    while(loop_flag){
        printf(">> ");
        fgets(msg, 40, stdin);
        args_number = sscanf(msg,"%s %s",args[0],args[1]);
        // arg_len = my_parser(&args,msg);
        if(args_number>0){
            command_index = find_command(args[0]);
        }else{
            command_index = -1;
        }
        switch(command_index){
            case 0://help
            printf(SERVER_HELP_MSG);
            break;
            case 1://showpeers
            peers_list_print();
            break;
            case 2://showneighbor
            if(args_number < 2){
                printf("Manca l'ID\n");
                break;
            }
            id = atoi(args[1]);
            if(id<0 || id>=peers_number){
                printf("ID non riconosciuto!\n");
            }
            peers_table_print_peer_neighbor(id);
            break;
            case 3://esc
            loop_flag = 0;
            printf("Chiusura in corso...\n");
            break;
            case 4://showpeersinfo
            peers_table_print_all_peers();
            break;
            default:
            printf("Comando non riconosciuto!\n");
            break;
        }

    }


}


int main(int argc, char* argv[]){
    pthread_t service_thread;
    void* thread_ret;
    globals_init();
    if(pthread_create(&service_thread,NULL,thread_loop,NULL)){
        perror("Errore nella creazione del thread");
        exit(EXIT_FAILURE);
    }
    user_loop();
    pthread_join(service_thread,&thread_ret);
    globals_free();
    printf("Ciao, ciao!\n");
   /* int s_id,c_id,len;
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
        }
        
    }*/

}