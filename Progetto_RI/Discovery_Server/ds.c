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

/*
DISCOVERY SERVER:
*/

pthread_mutex_t timer_mutex;
struct timer_elem{
    int id;
    int time_to_live;
    struct timer_elem* next;
};
struct timer_elem* timer_list;

/*
Controlla se il peer è nella lista del timer
*/
int timer_list_is_in(int id){
    struct timer_elem* elem;
    elem = timer_list;
    pthread_mutex_lock(&timer_mutex);
    while(elem){
        if(elem->id == id){
            pthread_mutex_unlock(&timer_mutex);
            return 1;
        } 
        elem = elem->next;
    }
    pthread_mutex_unlock(&timer_mutex);
    return 0;
}

/*
Controlla se il peer è nella lista del timer e lo rimuove da
quest'ultimo passandolo come valore di ritorno.
Questa funzione viene utilizzata:
-Per aggiornare il time_to_live del peer e reinserirlo in lista.
-Per rimuovere il peer definitivamente.
*/
struct timer_elem* timer_list_check_and_remove(int id){
    struct timer_elem* cur,*prev,*ret;
    if(!timer_list_is_in(id)) return NULL;
    prev = NULL;
    cur = timer_list;
    pthread_mutex_lock(&timer_mutex);
    while(cur){
        if(cur->id==id) break;
        prev = cur;
        cur= cur->next;
    }
    //Aggiorno puntatore next
    if(prev == NULL){
        timer_list = cur->next;
    }else{
        prev = cur->next;
    }
    ret = cur;
    cur = cur->next;
    //Aggiorno time to live degli elementi dopo all'elemento rimosso
    if(ret->time_to_live == 0){ //Niente da aggiornare
        pthread_mutex_unlock(&timer_mutex);
        return ret;
    } 
    while(cur){
        cur->time_to_live += ret->time_to_live;
        cur=cur->next;
    }
    pthread_mutex_unlock(&timer_mutex);
    return ret;

}
/*
Diminuisce di uno il time_to_live dell'elemento in testa a timer_list,
*/
void remove_peer(int id);
void timer_list_update(){
    if(timer_list==NULL) return;
    pthread_mutex_lock(&timer_mutex);
    timer_list->time_to_live--;
    pthread_mutex_unlock(&timer_mutex);
    /*
    Fintanto che gli elementi hanno time_to_live uguale a 0
    gli elimino dalla lista.
    */
    while(timer_list->time_to_live==0){
        remove_peer(timer_list->id);
    }
}

void timer_list_delete(int id){
    struct timer_elem* elem;
    elem = timer_list_check_and_remove(id);
    if(elem!=NULL) free(elem);
}

void timer_list_add(int id){
    struct timer_elem *elem,*cur,*prev;
    elem = timer_list_check_and_remove(id);
    if(elem == NULL){//Nuovo inserimento
        elem = malloc(sizeof(struct timer_elem));
    }
    elem->time_to_live = DEFAULT_TIME_TO_LIVE;
    cur = timer_list;
    prev = NULL;
    while(cur){
        elem->time_to_live-=cur->time_to_live;
        prev = cur;
        cur=cur->next;
    }
    if(prev == NULL){
        timer_list = elem;
    }else{
        prev->next = elem;
    }
    elem->next = NULL;
}


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
    timer_list = NULL;
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
    pthread_mutex_init(&timer_mutex,NULL);
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

void free_timer_list(){
    struct timer_elem* aux;
    while(timer_list!=NULL){
        aux = timer_list;
        timer_list = timer_list->next;
        free(aux);
    }
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
     pthread_mutex_lock(&table_mutex);
    for(i = 0 ; i<peers_table_size;i++){
        if(peers_table[i].port==-1){// trovata riga libera
            populate_peers_table_row(i,addr,port);
            pthread_mutex_unlock(&table_mutex);
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
        pthread_mutex_unlock(&table_mutex);
       return -1;
   }
   peers_table = (struct peer_des*)aux;
   for(int j = i+1 ; j<peers_table_size;j++){
       peers_table[j].port = -1;
   }
   populate_peers_table_row(i,addr,port);
   pthread_mutex_unlock(&table_mutex);
   return i;
}

void peers_table_remove_peer(int i){
    struct peer_des *pd;
    if(i>=peers_table_size){pthread_mutex_unlock(&table_mutex); return;}
    pd = &peers_table[i];
    if(pd->port == -1){pthread_mutex_unlock(&table_mutex);return;}
    pd->port = -1;
    free(pd->neighbors_vector);
    peers_number--;
    pthread_mutex_unlock(&table_mutex);
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
    pthread_mutex_lock(&table_mutex);
    if(id>=peers_table_size){pthread_mutex_unlock(&table_mutex);return -1;}
    pd = &peers_table[id];
    //riga vuota
    if(pd->port == -1){pthread_mutex_unlock(&table_mutex);return -1;}
    for(i = 0 ; i<pd->neighbors_vector_size;i++){
        if(pd->neighbors_vector[i]==-1){// trovata casella libera
            pd->neighbors_vector[i] = neighbour_id;
            pd->neighbors_number++;
            pthread_mutex_unlock(&table_mutex);
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
       pthread_mutex_unlock(&table_mutex);
       return -1;
   }
   pd->neighbors_vector = (int*)aux;
   for(int j = i+1 ; j<pd->neighbors_vector_size;j++){
       pd->neighbors_vector[j] = -1;
   }
   pd->neighbors_vector[i] = neighbour_id;
   pd->neighbors_number++;
   pthread_mutex_unlock(&table_mutex);
   return 1;
}

void peers_table_remove_neighbour(int id, int neighbour_id){
    struct peer_des *pd;
    pthread_mutex_lock(&table_mutex);
    if(id>=peers_table_size){pthread_mutex_unlock(&table_mutex);return;}
    pd = &peers_table[id];
    if(pd->port == -1){ pthread_mutex_unlock(&table_mutex);return;}
    for(int i = 0 ; i<pd->neighbors_vector_size;i++){
        if(pd->neighbors_vector[i] == neighbour_id){
            pd->neighbors_vector[i] = -1;
            pd->neighbors_number--;
            pthread_mutex_unlock(&table_mutex);
            return;
        }
    }
    pthread_mutex_unlock(&table_mutex);   
}

// Controlla se il peer id ha come vicino neighbour_id
int peers_table_has_neighbour(int id, int neighbour_id){
// Se id è uguale a neighbour_id restituisce vero
    if(id == neighbour_id) return 1;
    struct peer_des *pd;
    pthread_mutex_lock(&table_mutex);
    if(id>=peers_table_size) {pthread_mutex_unlock(&table_mutex);return 0;}
    pd = &peers_table[id];
    if(pd->port == -1) {pthread_mutex_unlock(&table_mutex);return 0;}
    for(int i = 0 ; i<pd->neighbors_vector_size;i++){
        if(pd->neighbors_vector[i] == neighbour_id){
            {pthread_mutex_unlock(&table_mutex);return 1;}
        }
    }
    pthread_mutex_unlock(&table_mutex);
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
    pthread_mutex_lock(&list_mutex);
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
    pthread_mutex_unlock(&list_mutex);
}

void peers_list_remove(int id){
    struct peer_elem *peer, *prev_peer;
    pthread_mutex_lock(&list_mutex);
    peer = peers_list;
    prev_peer = NULL;
    while(peer != NULL){
        if( peer->id != id)break;
        prev_peer = peer;
        peer = peer->next;
    }
    printf("list_remove uscito dal primo loop\n");
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
    pthread_mutex_unlock(&list_mutex);
}

/*
Se il peer in testa alla lista ha meno di due vicini,
scorro la lista in cerca del primo peer non nella sua lista dei vicini,
essendo la lista ordinata sarà anche il più vicino al livello di porta
*/
void print_peer_elem(struct peer_elem* pe);
void fix_head_isolation(){
    pthread_mutex_lock(&list_mutex);
    if(peers_number<2) {pthread_mutex_unlock(&list_mutex);return;}
    if(peers_list == NULL) {pthread_mutex_unlock(&list_mutex);return;}
    printf("prima di ->next\n");
    struct peer_elem *peer = peers_list->next;
    int id = peers_list->id;
    printf("descrittore...%d\n",id);
    struct peer_des *pd = get_peer_des(id);
    //Se possiede la quantità minima di vicini salto il controllo
    if(pd->neighbors_number>=MIN_NEIGHBOUR_NUMBER){pthread_mutex_unlock(&list_mutex);return;}
    printf("loop...\n");
    while(peer!=NULL){
        //printf("Sono %d e forse ho trovato un nuovo amichetto %d\n",id,peer->id);
        if(!peers_table_has_neighbour(id,peer->id)){
            //printf("Sono %d e ho trovato un nuovo amichetto %d\n",id,peer->id);
            peers_table_add_neighbour(id,peer->id);
            peers_table_add_neighbour(peer->id,id);
            if(pd->neighbors_number>=MIN_NEIGHBOUR_NUMBER){pthread_mutex_unlock(&list_mutex);return;}
        }
        peer = peer->next;
    }
    printf("fine loop...\n");
    pthread_mutex_unlock(&list_mutex);
}
/*
Se il peer in coda alla lista ha meno di due vicini,
scorro la lista ( al contrario ) in cerca del primo peer non nella sua lista dei vicini,
essendo la lista ordinata sarà anche il più vicino al livello di porta
*/
void fix_tail_isolation(){
    if(peers_number<2) {pthread_mutex_unlock(&list_mutex);return;}
    if(peers_list_tail == NULL) {pthread_mutex_unlock(&list_mutex);return;}
    struct peer_elem *peer = peers_list_tail->prev;
    int id = peers_list_tail->id;
    struct peer_des *pd = get_peer_des(id);
    //Se possiede la quantità minima di vicini salto il controllo
    if(pd->neighbors_number>=MIN_NEIGHBOUR_NUMBER){pthread_mutex_unlock(&list_mutex);return;}
    while(peer!=NULL){
        if(!peers_table_has_neighbour(id,peer->id)){
            peers_table_add_neighbour(id,peer->id);
            peers_table_add_neighbour(peer->id,id);
            if(pd->neighbors_number>=MIN_NEIGHBOUR_NUMBER){pthread_mutex_unlock(&list_mutex);return;}
        }
        peer = peer->prev;
    }
    pthread_mutex_unlock(&list_mutex);
}


void print_peer_elem(struct peer_elem* pe){
    printf("[ ID: %d , port: %d ]\n",pe->id,pe->port);
}
void peers_list_print(){
    struct peer_elem *peer;
    peer = peers_list;
    printf("Peers list:\n");
    while(peer != NULL){
        print_peer_elem(peer);
        peer = peer->next;
    }
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

   
    id = peers_table_add_peer(addr,port);
    pe = peer_elem_init(id,port);

    peers_list_add(pe);
    fix_head_isolation();
    fix_tail_isolation();
    timer_list_add(id);
    return id;
}
/*
Svuota la riga della peers_table identificata con l'id passato, elimina l'id
dalla lista dei vicini (neighbors_vector) e elimina il relativo peer_elem dalla
lista dei peer, ricalcolando i vicini e risolvendo eventuali problemi di 
isolamento dei peer in testa e coda della lista.
*/
void remove_peer(int id){
    printf("remove peer\n");
    peers_table_remove_peer(id);
    for(int i = 0 ; i<peers_table_size;i++){
        if(id == i)continue;
        printf("remove %d from %dr\n",id,i);
        peers_table_remove_neighbour(i,id);
    }
    printf("uscito loop entro in list_remove\n");
    peers_list_remove(id);
    printf("uscito, entro in fix head\n");
    fix_head_isolation();
    printf("uscito, entro in fix tail\n");
    fix_tail_isolation();
    printf("uscito, entro in timer_list_delete\n");
    timer_list_delete(id);
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
    pthread_mutex_lock(&table_mutex);
    char aux[250]="";
    struct peer_des* pd = get_peer_des(id);
    if(pd){
        struct peer_des* neighbour;
        sprintf(aux+strlen(aux),"%d,%d\n",id,pd->neighbors_number);
        for(int i = 0 ; i<pd->neighbors_vector_size;i++){
            if(pd->neighbors_vector[i]==-1)continue;
            neighbour = get_peer_des(pd->neighbors_vector[i]);
            sprintf(aux+strlen(aux),"%d,%u,%d\n",pd->neighbors_vector[i],neighbour->addr.s_addr,neighbour->port);
        }
        
        msg = malloc(sizeof(char)*(strlen(aux)+1));
        
        strcpy(msg,aux);
        pthread_mutex_unlock(&table_mutex);
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
void thread_test(){
    int port = 2000;
    struct in_addr addr;
    inet_pton(AF_INET,LOCAL_HOST,&addr);
    for(int i = 0; i<10;i++){
        add_peer(addr,port+i);
        sleep(1);
    }
}

void* thread_ds_loop(void* arg){
    //buffers
    char buffer[DS_BUFFER];
    char *msg=0;
    int msg_len;
    char option=0;
    //sockets info
    int ds_socket;
    socklen_t peer_addrlen;
    struct sockaddr_in ds_addr,peer_addr;
    int port = *(int*)arg;
    //peer info
    struct in_addr peer_in_addr;
    int peer_port=-1;
    int id;
    thread_test();
    if(open_udp_socket(&ds_socket,&ds_addr,port)){
        perror("[Thread]: Impossibile aprire socket\n");
        loop_flag = 0;
        pthread_exit(NULL);
    }
    printf("[Thread]: Socket UDP aperta alla porta: %d\n",port);
    peer_addrlen = sizeof(peer_addr);
    while(loop_flag){
        if(recvfrom(ds_socket,buffer,DS_BUFFER,0,(struct sockaddr*)&peer_addr,&peer_addrlen)<0){
            continue;
        }
        if(strcmp(buffer,"exit")==0){ 
            printf("[Thread]: Ricosciuto comando di uscita\n");
            continue;
        }
        scanf(buffer,"%u,%d,%c",peer_addr.sin_addr,peer_port,option);
        id = get_id_by_ip_port(peer_in_addr,peer_port);
        if(id==-1){//nuovo utente
            id = add_peer(peer_in_addr,peer_port);
        }else if(option == 'r'){//Richiesta di update
            
        }
        msg_len = generate_neighbors_list_message(id,msg);
        sendto(ds_socket,msg,msg_len,0,(struct sockaddr*)&peer_addr,peer_addrlen);
    }
    close(ds_socket);
    pthread_exit(NULL);
}

/*
Conta i secondi rimanenti al peer prima di essere eliminato,
elimina i peer con time_to_live uguale a zero
Il peer rinnova il suo time_to_live inviando un pacchetto apposito al DS
( gestito quindi dal thread_ds_loop )
*/

void * thread_timer_loop(void* arg){
    while(loop_flag){
        sleep(1);
        printf("sveglio\n");
        timer_list_update();
        printf("uscito da list_update\n");
    }
    return(NULL);
}


/*
### USER LOOP  e MAIN ##############################################
*/

//Controlla se il comando passato dall'utente è effettivamente uno di quelli
//riconosiuti, restituendo l'indice del comando ( -1 se non riconosciuto )
int find_command(char* command){
    char command_list[][SERVER_MAX_COMMAND_SIZE] = SERVER_COMMAND_LIST;
    for(int i = 0; i<SERVER_COMMANDS_NUMBER; i++){
        if(strcmp(command,command_list[i]) == 0 ) return i;
    }
    return -1;
}

//Sveglio il thread in ascolto per farlo uscire dal loop
void send_exit_packet(int ds_port){
    char msg[5] = "exit";
    int socket;
    socklen_t ds_addrlen;
    struct sockaddr_in ds_addr,socket_addr;
    if(open_udp_socket(&socket,&socket_addr,ds_port+1)){
        perror("Impossibile aprire socket");
        loop_flag = 0;
        exit(EXIT_FAILURE);
    }
    ds_addr.sin_family = AF_INET; //Tipo di socket
    ds_addr.sin_port = htons(ds_port);//Porta
    inet_pton(AF_INET,LOCAL_HOST,&ds_addr.sin_addr);
    ds_addrlen = sizeof(ds_addr);
    sendto(socket,msg,5,0,(struct sockaddr*)&ds_addr,ds_addrlen);
    close(socket);
}

/*
Loop che gestisce l'interfaccia utente
*/
void user_loop(int port){
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
        switch(command_index){//Gestione dei comandi riconosciuti
            //__help__
            case 0:
                printf(SERVER_HELP_MSG);
                break;
            //__showpeers__
            case 1:
                peers_list_print();
                break;
            //__showneighbor__
            case 2:
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
            //__esc__
            case 3:
                loop_flag = 0;
                printf("Chiusura in corso...\n");
                send_exit_packet(port);
                sleep(1);
            break;
            //__showpeersinfo__
            case 4:
                peers_table_print_all_peers();
            break;
            //__comando non riconosciuto__
            default:
                printf("Comando non riconosciuto!\n");
            break;
        }

    }


}


int main(int argc, char* argv[]){
    pthread_t service_thread,timer_thread;
    int port;
    void* thread_ret;
    globals_init();
    if(argc>1){
        port = atoi(argv[1]);
    }else{
        port = DEFAULT_PORT;
    }
    //thread_timer_loop
    if(pthread_create(&service_thread,NULL,thread_ds_loop,(void*)&port)){
        perror("Errore nella creazione del thread\n");
        exit(EXIT_FAILURE);
    }
    if(pthread_create(&timer_thread,NULL,thread_timer_loop,NULL)){
        perror("Errore nella creazione del thread\n");
        exit(EXIT_FAILURE);
    }
    sleep(1);
    user_loop(port);
    pthread_join(service_thread,&thread_ret);
    pthread_join(timer_thread,&thread_ret);
    globals_free();
    printf("Ciao, ciao!\n");
}