#include "../consts/socket_consts.h"
#include "../consts/ds_consts.h"
#include "../libs/my_sockets.h"
#include "../libs/my_logger.h"
#include <stdio.h>
#include <stdlib.h>
#include <arpa/inet.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <unistd.h>
#include <pthread.h>
#include <time.h>

/*
DISCOVERY SERVER:
*/
//Mutex che gestisce la concorrenzialità sulla time_list 
struct my_log *ds_log;
struct my_log *user_log;
struct my_log *timer_log;

pthread_mutex_t timer_mutex;

struct timer_elem{
    //Id del peer
    int id;
    //Secondi rimanenti di vita
    int time_to_live;
    //Puntatore al prossimo timer_elem
    struct timer_elem* next;
};
struct timer_elem* timer_list;



/**
 * @brief  Controlla se il peer è nella lista del timer
 * @note   
 * @param  id:int identificativo del peer 
 * @retval int 1 : presente ; 0 : non presente
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

/**
 * @brief  Controlla se il peer è nella timer_list e lo rimuove da
           quest'ultima
 * @note   usato per:
 *         -Aggiornare il time_to_live del peer e reinserirlo in lista.
           -Rimuovere il peer definitivamente. 
 * @param  id:int identificativo del peer da rimuovere
 * @retval timer_elem* puntatore al timer_elem rimosso
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
        prev->next = cur->next;
    }
    ret = cur;
    cur = cur->next;
    //Aggiorno time to live dell'elemento successivo
    if(ret->time_to_live == 0){ //Niente da aggiornare
        pthread_mutex_unlock(&timer_mutex);
        return ret;
    }
    if(cur) {
        cur->time_to_live += ret->time_to_live;
    }
    pthread_mutex_unlock(&timer_mutex);
    return ret;

}

/**
 * @brief  Controlla se la testa della coda timer_list ha time_to_live = 0
 * @note   
 * @retval int 1 : timer_elem scaduto , 0 : timer_elem non scaduto
 */
int timer_list_head_time_experide(){
    int ret;
    pthread_mutex_lock(&timer_mutex);
    if(!timer_list) return 0;
    ret = (timer_list->time_to_live==0);
    pthread_mutex_unlock(&timer_mutex);
    return ret;
}

void remove_peer(int id);
/**
 * @brief  Diminuisce di uno il time_to_live dell'elemento in testa a timer_list
 * @note   vedi remove_peer , timer_list_head_time_expired
 * @retval None
 */
void timer_list_update(){
    my_log_print(timer_log,"Controllo la timer_list...\n");
    pthread_mutex_lock(&timer_mutex);
    if(timer_list==NULL){
        my_log_print(timer_log,"La timer list è vuota!\n");
        pthread_mutex_unlock(&timer_mutex);
        return;
    }
    my_log_print(timer_log,"Decremento l'elemento in testa ID:%d TTL:%d\n",timer_list->id,timer_list->time_to_live);
    timer_list->time_to_live--;
    pthread_mutex_unlock(&timer_mutex);
    /*
    Fintanto che gli elementi hanno time_to_live uguale a 0
    gli elimino dalla lista.
    */
    my_log_print(timer_log,"Controllo i TTL scaduti\n");
    while(timer_list_head_time_experide()){
        //sleep(1);
        my_log_print(timer_log,"ID:%d ha TTL 0, rimuvo il peer\n",timer_list->id);
        remove_peer(timer_list->id); 
    }
}

/**
 * @brief  Stampa la timer_list
 * @note   
 * @retval None
 */
void timer_list_print(){
    struct timer_elem* cur;
    pthread_mutex_lock(&timer_mutex);
    cur = timer_list;
    if(cur == NULL){
        printf("timer_list vuota!\n");
        pthread_mutex_unlock(&timer_mutex);
        return;
    }
    while(cur){
        printf("[ %d,%d ]",cur->id,cur->time_to_live);
        cur = cur->next;
    }
    printf("\n");
    pthread_mutex_unlock(&timer_mutex);
}

/**
 * @brief  Rimuove dalla lista ( deallocando l'elemento estratto ) dalla timer_list
 * @note   vedi timer_list_check_and_remove
 * @param  id:int identificativo del peer 
 * @retval None
 */
void timer_list_delete(int id){
    struct timer_elem* elem;
    elem = timer_list_check_and_remove(id);
    if(elem!=NULL) free(elem);
}


//
/**
 * @brief  Aggiorna o aggiunge un timer_elem di id passato come argomento
 * @note   
 * @param  id:int identificativo del peer
 * @retval None
 */
void timer_list_add(int id){
    struct timer_elem *elem,*cur,*prev;
    elem = timer_list_check_and_remove(id);
    if(elem == NULL){//Nuovo inserimento
        elem = malloc(sizeof(struct timer_elem));
        elem->id = id;
    }
    elem->time_to_live = DEFAULT_TIME_TO_LIVE;
    elem->next = NULL;
    pthread_mutex_lock(&timer_mutex);
    cur = timer_list;
    prev = NULL;
    while(cur){
        if(elem->time_to_live==0)break;
        if(elem->time_to_live<cur->time_to_live)break;
        elem->time_to_live-=cur->time_to_live;
        prev = cur;
        cur=cur->next;
    }
    if(prev == NULL){
        timer_list = elem;
    }else{
        prev->next = elem;
    }
    if(cur!=NULL){
        elem->next = cur;
    }else{
        elem->next = NULL;
    }
    pthread_mutex_unlock(&timer_mutex);
}
//mutex per gestire la concorrenza della variabile sync_time
pthread_mutex_t sync_time_mutex;
time_t sync_time;
/**
 * @brief  Aggiunge un giorno a sync_time
 * @note   
 * @retval None
 */
void sync_time_add_day(){
    pthread_mutex_lock(&sync_time_mutex);
    //Aggiungo un giorno
    sync_time+= 86400;
    pthread_mutex_unlock(&sync_time_mutex);
}
/**
 * @brief  Setta la variabile sync_time
 * @note   
 * @param  t:time_t la nuova data da associare a sync_time
 * @retval None
 */
void sync_time_set(time_t t){
    pthread_mutex_lock(&sync_time_mutex);
    //Aggiungo un giorno
    sync_time=t;
    pthread_mutex_unlock(&sync_time_mutex);
}
/**
 * @brief  Restituisce una copia di sync_time
 * @note   
 * @retval ;time_t il valore di sync_time
 */
time_t sync_time_get(){
    time_t ret;
    pthread_mutex_lock(&sync_time_mutex);
    ret = sync_time;
    pthread_mutex_unlock(&sync_time_mutex);
    return ret;
}
/**
 * @brief  Salva su file il valore di sync_time
 * @note   guarda SYNC_TIME_PATH in consts per il nome del file
 * @retval None
 */
void sync_time_save(){
    FILE *f;
    pthread_mutex_lock(&sync_time_mutex);
    f = fopen(SYNC_TIME_PATH,"w");
    fprintf(f,"%ld",sync_time);
    fclose(f);
    pthread_mutex_unlock(&sync_time_mutex);
}
/**
 * @brief  Carica sync_time da file, se non esiste lo genera basandosi sulla data corrente
 * @note   guarda SYNC_TIME_PATH in consts per il nome del file
 * @retval time_t il valore di sync_time
 */
time_t sync_time_load(){
    FILE *f;
    time_t t;
    struct tm *tm_sync;
    int ret;
    f = fopen(SYNC_TIME_PATH,"r");
    pthread_mutex_lock(&sync_time_mutex);
    if(f){
        ret = fscanf(f,"%ld",&sync_time);
        if(ret==0){//File vuoto!
           goto make_time; 
        }
    }else{
make_time:
        time(&sync_time);
        tm_sync = localtime(&sync_time);
        tm_sync->tm_sec = 0;
        tm_sync->tm_min = 0;
        tm_sync->tm_hour = 0;
        sync_time = mktime(tm_sync);
        printf("Sync_time = %s",ctime(&sync_time));
    }
    t = sync_time;
    if(f)fclose(f);
    pthread_mutex_unlock(&sync_time_mutex);
    return t;
}
/*
Elemento che definisce un peer in una lista ordinata
*/
struct peer_elem{
    //Id del peer ( indice della peers_table )
    int id;
    //porta del peer ( utilizzata per l'ordinamento )
    int port;
    //Prossimo elemento
    struct peer_elem* next;
    //Elemento precedente
    struct peer_elem* prev;
};
//Puntatore alla testa della lista di peer_elem
struct peer_elem* peers_list;
//Puntatore alla coda della lista di peer_elem
struct peer_elem* peers_list_tail;
pthread_mutex_t list_mutex;

/*
Descrittore di un peer
*/
struct peer_des{
    // Indirizzo del peer
    struct in_addr addr;
    // Porta del peer
    int port;
    //Numero dei vicini
    int neighbors_number;
    //Dimensione del neighbors_vector 
    int  neighbors_vector_size;
    //Array dinamico degli id dei vicini
    int* neighbors_vector;
};
//Il numero dei peer attualmente nelle rete
int peers_number;
//La grandezza della peers_table
int peers_table_size; 
//Tabella dei descrittori di peer
struct peer_des* peers_table; 
pthread_mutex_t table_mutex;

/**
 * @brief Restituisce un puntatore al descritore di peer con l'id passato ( se esiste )
 * @note   
 * @param  id: identificativo del peer 
 * @retval peer_des* puntatore al descrittore di peer, NULL se non esiste l'elemento
 */
struct peer_des* get_peer_des(int id){
    if(id >= peers_table_size){
        return NULL;
    }  // Indice troppo altro rispetto alla grandezza della tabella
    if(peers_table[id].port == -1){
        return NULL;
    }  // elemento vuoto della peers_table
    return &peers_table[id];
}



/**
 * @brief  Stampa le informazioni del peer con l'id passa ( se esiste )
 * @note   
 * @param  id:int identificativo del peer
 * @retval None
 */
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
/**
 * @brief  Stampa la lista dei vicini del peer passato
 * @note   
 * @param  id:int identificativo del peer
 * @retval None
 */
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
/**
 * @brief  Stampa tutte le informazioni dei peer presenti in rete
 * @note   guarda peer_table_print_peer
 * @retval None
 */
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
void logs_init(){
    char* path;
    char* name;
    name = malloc(strlen(DEFAULT_DS_THREAD_NAME)+1);
    strcpy(name,DEFAULT_DS_THREAD_NAME);
    path = malloc(strlen(DEFAULT_DS_COMUNITCATION_LOG_FILENAME)+1);
    
    strcpy(path,DEFAULT_DS_COMUNITCATION_LOG_FILENAME);
    ds_log = my_log_init(path,name);
    free(path);
    free(name);

    name = malloc(strlen(DEFAULT_TIMER_THREAD_NAME)+1);
    strcpy(name,DEFAULT_TIMER_THREAD_NAME);
    path = malloc(strlen(DEFAULT_TIMER_COMUNICATION_LOG_FILENAME)+1);
    
    strcpy(path,DEFAULT_TIMER_COMUNICATION_LOG_FILENAME);
    timer_log = my_log_init(path,name);
    free(path);
    free(name);

    name = malloc(strlen(DEFAULT_USER_THREAD_NAME)+1);
    strcpy(name,DEFAULT_USER_THREAD_NAME);
    path = malloc(strlen(DEFAULT_USER_LOG_FILENAME)+1);
    
    strcpy(path,DEFAULT_USER_LOG_FILENAME);
    user_log = my_log_init(path,name);
    free(path);
    free(name);

}
void logs_free(){
    my_log_free(ds_log);
    my_log_free(timer_log);
    my_log_free(user_log);
}




//Gestisce il loop dei thread
int loop_flag;
/**
 * @brief  Inizializza tutte le variabili globali
 * @note   
 * @retval None
 */
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
    sync_time = 0;
    pthread_mutex_init(&list_mutex,NULL);
    pthread_mutex_init(&table_mutex,NULL);
    pthread_mutex_init(&timer_mutex,NULL);
    logs_init();
}

/**
 * @brief  Dealloca la peers_table
 * @note   
 * @retval None
 */
void free_peers_table(){
    for(int i = 0 ; i<peers_table_size; i++){
        if(peers_table[i].port == -1) continue;
        free(peers_table[i].neighbors_vector);
    }
    free(peers_table);
}

/**
 * @brief  Dealloca la peers_list
 * @note   
 * @retval None
 */
void free_peers_list(){
    struct peer_elem* aux;
    while(peers_list!=NULL){
        aux = peers_list;
        peers_list = peers_list->next;
        free(aux);
    }
    peers_list_tail = NULL;
}

/**
 * @brief  Dealloca la timer_list
 * @note   
 * @retval None
 */
void free_timer_list(){
    struct timer_elem* aux;
    while(timer_list!=NULL){
        aux = timer_list;
        timer_list = timer_list->next;
        free(aux);
    }
}
/**
 * @brief  Dealloca tutte la variabili globali dinamiche
 * @note   
 * @retval None
 */
void globals_free(){
    free_peers_list();
    free_peers_table();
    free_timer_list();
    logs_free();
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
/**
 * @brief  Aggiunge le info di un peer nella peers_table
 * @note   
 * @param  i:int indice della tabella ( id del peer )
 * @param  addr:in_addr address del peer
 * @param  port:int porta della socket tcp del peer
 * @retval None
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
/**
 * @brief  Restituisce l'id del peer (indice della tabella dei descrittori di peer) 
           dopo aver allocato il descrittore del nuovo peer
 * @note   
 * @param  addr:in_addr address del peer
 * @param  port:int porta della socket tcp del peer 
 * @retval 
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
   aux = realloc(peers_table,sizeof(struct peer_des)*peers_table_size);
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
/**
 * @brief  Rimuove dalla lista il peer di indice i, dealloca la lista dei vicini ( neighbors_vector )
 * @note   
 * @param  i:int indice della tabella ( id del peer )
 * @retval None
 */
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

/**
 * @brief  Aggiunge un vicino al vettore dinamico dei vicini ( neighbour_vector )
 * @note   
 * @param  id:int id del peer a cui aggiungere il vicino
 * @param  neighbour_id:int id del nuovo vicino
 * @retval 
 */
int peers_table_add_neighbour(int id , int neighbour_id){
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
   aux = realloc(pd->neighbors_vector,sizeof(int)*pd->neighbors_vector_size);
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

/**
 * @brief  Rimuove un vicino dalla lista dei vicini del peer passato
 * @note   
 * @param  id:int identificativo del peer
 * @param  neighbour_id:int identificativo del vicino da rimuovere 
 * @retval None
 */
void peers_table_remove_neighbour(int id, int neighbour_id){
    struct peer_des *pd;
    pthread_mutex_lock(&table_mutex);
    if(id>=peers_table_size || neighbour_id>=peers_table_size){pthread_mutex_unlock(&table_mutex);return;}
    pd = &peers_table[id];
    if(pd->port == -1){pthread_mutex_unlock(&table_mutex);return;}
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

/**
 * @brief  Controlla se il peer id ha come vicino neighbour_id
 * @note   
 * @param  id:int identificativo del peer
 * @param  neighbour_id:int identificativo del vicino
 * @retval 
 */
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
/**
 * @brief  Restituisce l'id del peer basandosi su indirizzo e porta
 * @note   
 * @param  addr:in_addr indirizzo del peer
 * @param  port:int porta della socket tcp del peer
 * @retval 
 */
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

/**
 * @brief  inizializza un peer_elem e lo restituisce
 * @note   
 * @param  id:int identificativo del peer 
 * @param  port:int porta della socket tcp del peer
 * @retval peer_elem* puntatore al peer_elem allocato
 */
struct peer_elem* peer_elem_init(int id,int port){
    struct peer_elem* pe = malloc(sizeof(struct peer_elem));
    pe->id = id;
    pe->port = port;
    pe->next = NULL;
    pe->prev = NULL;
    return pe;
}
/**
 * @brief  Aggiunge un peer_elem alla peers_list
 * @note   
 * @param  new_peer:peer_elem* puntatore al peer_elem da inserire in lista
 * @retval None
 */
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
/**
 * @brief  Rimuove un peer_elem dalla peers_list basandosi sull'id
 * @note   
 * @param  id:int identificativo del peer da rimuovere
 * @retval None
 */
void peers_list_remove(int id){
    struct peer_elem *peer, *prev_peer;
    pthread_mutex_lock(&list_mutex);
    peer = peers_list;
    prev_peer = NULL;
    while(peer != NULL){
        if( peer->id == id)break;
        prev_peer = peer;
        peer = peer->next;
    }
    if(peer==NULL){//niente da eliminare
        pthread_mutex_unlock(&list_mutex);
        return;
    }else if(prev_peer==NULL){
        peers_list = peer->next;
        if(peer->next !=NULL) peer->next->prev = NULL;
    } else{
        prev_peer->next = peer->next;
        if(peer->next!=NULL){
            peer->next->prev = prev_peer;
            
            //Aggiorno la loro row nella peers_table
            // Il precedente e il successivo peer del peer eliminato potrebbero
            // Diventare nuovi vicini (se non lo sono già)
            if(!peers_table_has_neighbour(prev_peer->id,peer->next->id)){
                peers_table_add_neighbour(prev_peer->id,peer->next->id);
            }
            if(!peers_table_has_neighbour(peer->next->id,prev_peer->id)){
                peers_table_add_neighbour(peer->next->id,prev_peer->id);
            }
        }else{
            peers_list_tail = prev_peer;
        }
    }
    free(peer);
    pthread_mutex_unlock(&list_mutex);
}

/*

*/
void print_peer_elem(struct peer_elem* pe);
/**
 * @brief   Compensa l'eventuale isolamento del peer in testa alla lista
 * @note    Se il peer in testa alla lista ha meno di due vicini,
            scorro la lista in cerca del primo peer non nella sua lista dei vicini,
            essendo la lista ordinata sarà anche il più vicino al livello di porta 
 * @retval None
 */
void fix_head_isolation(){
    pthread_mutex_lock(&list_mutex);
    if(peers_number<2) {pthread_mutex_unlock(&list_mutex);return;}
    if(peers_list == NULL) {pthread_mutex_unlock(&list_mutex);return;}
    struct peer_elem *peer = peers_list->next;
    int id = peers_list->id;
    struct peer_des *pd = get_peer_des(id);
    if(pd->neighbors_number>=MIN_NEIGHBOUR_NUMBER){pthread_mutex_unlock(&list_mutex);return;}
    while(peer!=NULL){
        if(!peers_table_has_neighbour(id,peer->id)){
            peers_table_add_neighbour(id,peer->id);
            peers_table_add_neighbour(peer->id,id);
            if(pd->neighbors_number>=MIN_NEIGHBOUR_NUMBER){pthread_mutex_unlock(&list_mutex);return;}
        }
        peer = peer->next;
    }
    pthread_mutex_unlock(&list_mutex);
}

/**
 * @brief  Compensa l'eventuale isolamento del peer in coda alla lista
 * @note    Se il peer in coda alla lista ha meno di due vicini,
            scorro la lista ( al contrario ) in cerca del primo peer non nella sua lista dei vicini,
            essendo la lista ordinata sarà anche il più vicino al livello di porta
 * @retval None
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

/**
 * @brief  Stampa le informazioni del peer_elem passato
 * @note   
 * @param  pe:peer_elem* puntatore al peer_elem da stampare
 * @retval None
 */
void print_peer_elem(struct peer_elem* pe){
    printf("[%d,%d]",pe->id,pe->port);
}
/**
 * @brief  Stampa la peers_list
 * @note   vedi print_peer_elem
 * @retval None
 */
void peers_list_print(){
    struct peer_elem *peer;
    peer = peers_list;
    if(peer == NULL){
        printf("Nessun elemento in lista\n");
        return;
    }else{
        printf("Peers list\n");
    }
    while(peer != NULL){
        print_peer_elem(peer);
        peer = peer->next;
    }
    printf("\n");
}




/*
### FUNZIONI THREAD ##############################################
*/

/*

*/
/**
 * @brief  Aggiunge un peer alla rete
 * @note    Aggiunge una nuova riga alla tabbela dei descrittori di peer (peers_table)
            restituendo l'indice della tabella (id:int), viene allocato un peer_elem contenente
            l'indice della peers_table e la porta utilizzata per ordinare la lista (peers_list) nella
            quale sarà inserito.
            Effettuato l'inserimento verranno inidividuati i vicini e risolti gli eventuali
            problemi di isolamento della coda e della testa della lista.
 * @param  addr:in_addre indirizzo del peer
 * @param  port:int porta della socket tcp del peer
 * @retval 
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

*/
/**
 * @brief  Rimuove un peer dalla rete
 * @note    Svuota la riga della peers_table identificata con l'id passato, elimina l'id
            dalla lista dei vicini (neighbors_vector) e elimina il relativo peer_elem dalla
            lista dei peer, ricalcolando i vicini e risolvendo eventuali problemi di 
i           solamento dei peer in testa e coda della lista.
 * @param  id:int identificativo del peer da rimuovere
 * @retval None
 */
void remove_peer(int id){
    peers_table_remove_peer(id);
    for(int i = 0 ; i<peers_table_size;i++){
        if(id == i)continue;
        peers_table_remove_neighbour(i,id);
    }
    peers_list_remove(id);
    fix_head_isolation();
    fix_tail_isolation();
    timer_list_delete(id);
}

/*

Formato messaggio:

<id richiedente>,<numero di vicini>
<id>,<addr>,<porta>
...
<id>,<addr>,<porta>

*/
/**
 * @brief  Genera la lista dei vicini di id e restituisce la dimensione della nuova stringa msg allocata
 * @note    <id richiedente>,<numero di vicini>
            <id>,<addr>,<porta>
            ...
            <id>,<addr>,<porta>
 * @param  id:int identificatore del peer
 * @param  msg:char** puntatore alla stringa 
 * @retval 
 */
uint16_t generate_neighbors_list_message(int id,char** msg){
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
        
        *msg = malloc(sizeof(char)*(strlen(aux)+1));
        strcpy(*msg,aux);
        //printf("La lista dei vicini:\n%s",*msg);
        pthread_mutex_unlock(&table_mutex);
        return sizeof(aux)+1;
    }else{
        pthread_mutex_unlock(&table_mutex);
        return 0;
    }
}

/*
Il compito del thread è quello di aspettare richieste dai peer
e soddisfarle gestendo la peers_list, la peers_table e la timer_list
Cosa può fare:
- Aggiungere un peer
- Rimuovere un peer
- Inviare la lista dei vicini a un peer
- Aggiornare la timer_list
*/


void thread_test(){
    struct timer_elem* tm;
    int port = 2000;
    struct in_addr addr;
    inet_pton(AF_INET,LOCAL_HOST,&addr);
    for(int i = 0; i<10;i++){
        add_peer(addr,port+i);
        sleep(1);
    }
    for(int i = 9; i>5;i--){
        tm = timer_list_check_and_remove(i);
        if(tm){
            timer_list_add(i);
            sleep(1);
        }
        
    }
    printf("Test finto\n");
}

/**
 * @brief  Il compito del thread è quello di aspettare richieste dai peer
            e soddisfarle gestendo la peers_list, la peers_table e la timer_list.
 * @note   Cosa può fare:
            - Aggiungere un peer
            - Rimuovere un peer
            - Inviare la lista dei vicini a un peer
            - Aggiornare la timer_list
 * @param  arg:void* puntatore agli args ( in questo caso la porta di ascolto della socket udp )
 * @retval None
 */
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
    time_t time_recived;
    time_t cur_sync_time;
    int id;
    my_log_print(ds_log,"Apertura della socket alla porta: %d\n",port);
    cur_sync_time = sync_time_load();
    if(open_udp_socket(&ds_socket,&ds_addr,port)){
        perror("[DS_Thread]: Impossibile aprire socket");
        my_log_print(ds_log,"Impossibile aprire la socket alla porta: %d, chiusura in corso...\n",port);
        loop_flag = 0;
        pthread_exit(NULL);
    }
    my_log_print(ds_log,"Socket aperta!\n");
    printf("[DS_Thread]: Socket UDP aperta alla porta: %d\n",port);
    peer_addrlen = sizeof(peer_addr);
    //thread_test();
    while(loop_flag){
        
        if(recvfrom(ds_socket,buffer,DS_BUFFER,0,(struct sockaddr*)&peer_addr,&peer_addrlen)<0){
            perror("[DS_Thread]: Errore nella ricezione");
            my_log_print(ds_log,"Errore nella ricezione del pacchetto\n");
            continue;
        }
        if(strcmp(buffer,"exit")==0){ 
            printf("[DS_Thread]: Ricosciuto comando di uscita\n");
            my_log_print(ds_log,"Riconosciuto comando di uscita\n");
            continue;
        }
        my_log_print(ds_log,"Messaggio ricevuto: %s\n",buffer);
        sscanf(buffer,"%u,%d,%ld,%c",&peer_in_addr.s_addr,&peer_port,&time_recived,&option);
        id = get_id_by_ip_port(peer_in_addr,peer_port);
        if(id==-1){//nuovo utente
            id = add_peer(peer_in_addr,peer_port);
            my_log_print(ds_log,"Nuovo utente, id associato: %d \n",id);
        }else{//Refresha il time_to_live del peer
            my_log_print(ds_log,"Aggiorno TTL di %d\n",id);
            timer_list_add(id);
        }
        if(option =='r'){//Segnale di sincronizzazione
            my_log_print(ds_log,"Arrivato un messaggio di sincronizzazione\n");
            cur_sync_time = sync_time_get();
            if(cur_sync_time>time_recived){//Informo che necessita sincronizzarsi
                my_log_print(ds_log,"%d non è sincronizzato, invio la sync_time\n",id);
                sprintf(buffer,"%ld",cur_sync_time);
                sendto(ds_socket,buffer,(strlen(buffer)+1),0,(struct sockaddr*)&peer_addr,peer_addrlen);
            }else{
                my_log_print(ds_log,"%d è già sincronizzato, non invio niente\n",id);
            }
                /*else if(cur_sync_time<time_recived){
                sync_time_set(time_recived);
                printf("Aggiorno la data sync_time! %ld\n",sync_time);
            }*/
        }else if(option == 'x'){// Se x richiede anche la lista dei vicini
            my_log_print(ds_log,"%d richiede la lista dei suoi vicini\n",id);
            msg_len = generate_neighbors_list_message(id,&msg);
            my_log_print(ds_log,"Il messaggio che invierò a %d:\n%s\n",id,msg);
            sendto(ds_socket,msg,msg_len,0,(struct sockaddr*)&peer_addr,peer_addrlen);
        }else if(option == 'b'){// Bye Bye message, rimuovo il peer
            my_log_print(ds_log,"%d ci saluta, lo elimino dalla rete\n",id);
            remove_peer(id);
        }
        
         
    }
    my_log_print(ds_log,"Chiusura della socket\n");
    close(ds_socket);
    my_log_print(ds_log,"Salvo sync_time\n");
    sync_time_save(cur_sync_time);
    my_log_print(ds_log,"FINE\n");
    pthread_exit(NULL);
}
/**
 * @brief  Controlla se è l'ora di chiudere il registro odierno
 * @note   
 * @retval None
 */
void check_closing_hour(){
    struct tm *tm_close;
    time_t close;
    time_t now;
    my_log_print(timer_log,"Controllo se è l'ora di chiudere\n");
    time(&now);
    close = sync_time_get();
    tm_close = localtime(&close);
    tm_close->tm_hour = END_REG_HOUR;
    tm_close->tm_min = END_REG_MINUTES;
    close = mktime(tm_close);
    //printf("%s,%s\n",ctime(&now),ctime(&close));
    if(now>=close){
        my_log_print(timer_log,"Orario di chiusura = %s\n",ctime(&close));
        my_log_print(timer_log,"Orario di adesso = %s\n",ctime(&now));
        my_log_print(timer_log,"Aggiorno sync_time aggiungendo un giorno!\n",ctime(&close));
        sync_time_add_day();
    }else{
        my_log_print(timer_log,"Niente da aggiornare...\n");
    }
}

/*


*/
/**
 * @brief  Conta i secondi rimanenti al peer prima di essere eliminato,
           elimina i peer con time_to_live uguale a zero
 * @note   Il peer rinnova il suo time_to_live inviando un pacchetto apposito al DS
            ( gestito quindi dal thread_ds_loop )
 * @param  arg: nessun arg gestito ( NULL )
 * @retval 
 */
void * thread_timer_loop(void* arg){
    printf("[Timer_Thread]: ready.\n");
    my_log_print(timer_log,"Pronto\n");
    while(loop_flag){
        sleep(1);
        timer_list_update();
        check_closing_hour();
    }
    my_log_print(timer_log,"Chiuso\n");
    printf("[Timer_thread]: chiuso\n");
    return(NULL);
}


/*
### USER LOOP  e MAIN ##############################################
*/

/**
 * @brief   Controlla se il comando passato dall'utente è effettivamente uno di quelli
            riconosiuti, restituendo l'indice del comando ( -1 se non riconosciuto ) 
 * @note   
 * @param  command:char* stringa del commando ricevuto
 * @retval int indice del comando, -1 se il comando non è stato riconosciuto
 */
int find_command(char* command){
    char command_list[][SERVER_MAX_COMMAND_SIZE] = SERVER_COMMAND_LIST;
    for(int i = 0; i<SERVER_COMMANDS_NUMBER; i++){
        if(strcmp(command,command_list[i]) == 0 ) return i;
    }
    return -1;
}


/**
 * @brief Sveglia il thread in ascolto per farlo uscire dal loop 
 * @note   
 * @param  ds_port:int porta della socket udp
 * @retval None
 */
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
/**
 * @brief  Loop che gestisce l'interfaccia utente
 * @note   int port serve per inviare un pacchetto alla socket udp con il comando di interruzione
 * @param  port:int porta della socket udp in ascolto
 * @retval None
 */
void user_loop(int port){
    char msg[40];
    int id;
    int args_number;
    int command_index;
    char args[2][SERVER_MAX_COMMAND_SIZE];
    my_log_print(user_log,"Stampo messaggio di benvenuto...\n");
    printf(SERVER_WELCOME_MSG);
    while(loop_flag){
        printf(">> ");
        fgets(msg, 40, stdin);
        args_number = sscanf(msg,"%s %s",args[0],args[1]);
        //args_number = scanf("%s",args[0],args[1]);
        // arg_len = my_parser(&args,msg);
        my_log_print(user_log,"Stringa utente: %s\n",msg);
        if(args_number>0){
            command_index = find_command(args[0]);
        }else{
            command_index = -1;
        }
        switch(command_index){//Gestione dei comandi riconosciuti
            //__help__
            case 0:
                my_log_print(user_log,"Riconosciuto comando Help\n");
                printf(SERVER_HELP_MSG);
                break;
            //__showpeers__
            case 1:
                my_log_print(user_log,"Riconosciuto comando Showpeers\n");
                peers_list_print();
                break;
            //__showneighbor__
            case 2:
                my_log_print(user_log,"Riconosciuto comando Showneighbor\n");
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
                my_log_print(user_log,"Riconosciuto comando Esc\n");
                my_log_print(user_log,"Setto loop_flag a 0\n");
                loop_flag = 0;
                printf("Chiusura in corso...\n");
                my_log_print(user_log,"Invio il pacchetto di uscita alla socket udp\n");
                send_exit_packet(port);
                sleep(1);
            break;
            //__showpeersinfo__
            case 4:
                my_log_print(user_log,"Riconosciuto comando Showpeersinfo\n");
                timer_list_print();
                //peers_table_print_all_peers();
            break;
            //__add_day__
            case 5:
                my_log_print(user_log,"Riconosciuto comando Sync_time add day\n");
                printf("Segnalazione in corso...");
                my_log_print(user_log,"Aggiungo un giorno al sync_time\n");
                sync_time_add_day();
                sleep(1);
                printf("Fatto!\n");
                //peers_table_print_all_peers();
            break;
            //__comando non riconosciuto__
            default:
                my_log_print(user_log,"Nessun comando riconosciuto, stampo errore\n");
                printf("Comando non riconosciuto!\n");
            break;
        }

    }


}


int main(int argc, char* argv[]){
    pthread_t service_thread,timer_thread;
    int port;
    void* thread_ret;
    //Inzializzo le variabili globali
    globals_init();
    if(argc>1){
        port = atoi(argv[1]);
    }else{
        port = DEFAULT_PORT;
    }
    //Lancio il thread per comunicare con i peer
    if(pthread_create(&service_thread,NULL,thread_ds_loop,(void*)&port)){
        perror("Errore nella creazione del thread\n");
        exit(EXIT_FAILURE);
    }
    //Lancio il thread del timer per gestire il time_to_live dei peer
    if(pthread_create(&timer_thread,NULL,thread_timer_loop,NULL)){
        perror("Errore nella creazione del thread\n");
        exit(EXIT_FAILURE);
    }
    sleep(1);
    //Salto alla gestione dell'interfaccia utente
    if(loop_flag) user_loop(port);
    my_log_print(user_log,"In attesa del Service Thread\n");
    pthread_join(service_thread,&thread_ret);
    my_log_print(user_log,"In attesa del Timer Thread\n");
    pthread_join(timer_thread,&thread_ret);
    my_log_print(user_log,"FINE\n");
    globals_free();
    printf("Ciao, ciao!\n");
}