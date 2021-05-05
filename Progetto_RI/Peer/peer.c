
#include "../consts/const.h"
#include "../libs/my_sockets.h"
#include "../libs/my_parser.h"
#include <stdio.h>
#include <stdlib.h>
#include <arpa/inet.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <netinet/in.h>
#include <unistd.h>
#include <pthread.h>
#include <sys/stat.h>
#include <dirent.h>

/*
### Neighbors struct ############################
*/
//L'identificatore fornito dal DS
int my_id;
//La porta di ascolto del servizio TCP
int my_port;
//L'address del peer (Local host di default)
struct in_addr my_addr;
//Flag per i loop dei thread ( tcp, ds e user )
int loop_flag;
//FD la socket più grande
int max_socket;
//Socket del server
int s_socket;
//Il path della cartella del peer
char* my_path;
//Il path della cartella di log ( o del file di log )
char* my_log_path;
//Data del registro attualmente aperto
time_t current_open_date;
//FD set contenente tutte le socket
fd_set master;
//FD set contenente le socket da leggere
fd_set to_read;
//Mutex per garantire mutualità esclusiva a agli fd_set
pthread_mutex_t fd_mutex;
////Mutex per garantire mutualità esclusiva al char contentente l'opzione da richiede al DS
pthread_mutex_t ds_mutex;
pthread_mutex_t register_mutex;
pthread_t ds_comunication_thread;
pthread_t tcp_comunication_thread;
struct neighbour{
    int id;
    struct sockaddr_in addr;
    int socket;
    struct neighbour* next;
};
pthread_mutex_t neighbors_list_mutex;
int neighbors_number;
struct neighbour* neighbors_list;

struct flooding_mate{
    //Identifica l'elemento insieme a socket
    int id;
    //Identifica l'elemento insieme a id
    int socket;
    //Indica se abbiamo ricevuto una risposta dalla socket oppure no
    int recieved_flag;
    //Il prossimo elemento nella lista
    struct flooding_mate* next;

};

struct request{
    //Insieme a timestamp identifica la richiesta
    int id;
    //Insieme a id identifica la richiesta
    time_t timestamp;
    //La socket alla quale inviare il risultato completato il flooding;
    int ret_socket;
    //Tipo richiesta
    char request_type;
    //Data inizio
    time_t date_start;
    //Data fine
    time_t date_end;
    //La lista delle socket dalle quali attendo una risposta
    struct flooding_mate* flooding_list;
    //Il prossimo elemento in lista
    struct request* next;
};
struct request* requestes_list;

/**
 * @brief   Genera una lista di flooding_mate basata sui peer vicini
 * @note   Guarda neighbors_list , init_request
 * @retval 
 */
struct flooding_mate* init_flooding_list(){
    struct flooding_mate *head;
    struct flooding_mate *f;
    struct flooding_mate *prev;
    struct neighbour *n;
    head = NULL;
    prev = NULL;
    pthread_mutex_lock(&neighbors_list_mutex);
    n = neighbors_list;
    while(n){
        f = malloc(sizeof(struct flooding_mate));
        f->id = n->id;
        f->recieved_flag = 0;
        f->socket = n->socket;
        f->next = NULL;
        
        if(head==NULL){
            head = f;
        }
        if(prev!=NULL){
            prev->next = f;
        }
        prev = f;
        n = n->next;
    }
    pthread_mutex_unlock(&neighbors_list_mutex);
    return head;
}

/**
 * @brief  Dealloca la lista passata
 * @note   
 * @param  fm:flooding_mate* lista da liberare 
 * @retval None
 */
void free_flooding_list(struct flooding_mate* fm){
    struct flooding_mate *prev;
    while(fm){
        prev = fm;
        fm = fm->next;
        free(prev);
    }
}

/**
 * @brief  controlla se tutti gli elementi della lista hanno recieved_flag a 1
 * @note   
 * @param  fm:flooding_mate* lista da controllare 
 * @retval int 1 se sono tutti flaggati, 0 altrimenti
 */
int flooding_list_all_checked(struct flooding_mate *fm){
    while(fm){
        if(!fm->recieved_flag)return 0;
    }
    return 1;
}

/**
 * @brief  Flagga il recieved_flag del peer cercato se presente nella lista fm
 * @note   
 * @param  fm:flooding_mate* 
 * @param  socket:int   socket del peer da flaggare 
 * @param  id:int id del peer da flaggare
 * @retval int 1 l'operazione ha avuto successo, 0 altrimenti
 */
int flooding_list_check_flooding_mate(struct flooding_mate *fm,int socket,int id){
    while(fm){
        if(fm->socket==socket && fm->id==id){
            fm->recieved_flag = 1;
            return 1;
        }
    }
    return 0;
}
int requestes_list_add(struct request *e){
    struct request *cur;
    struct request *prev;
    prev = NULL;
    cur = requestes_list;
    while(cur){
        if(cur->timestamp>e->timestamp)break;
        prev = cur;
        cur = cur->next;
    }
    if(prev==NULL){
        requestes_list = e;
    }else{      
        prev->next = e;
    }
    e->next = cur;
    return 1;
}

struct request* requestes_list_get(time_t t,int id){
    struct request *cur;
    cur = requestes_list;
    while(cur){
        if(cur->timestamp == t && cur->id==id)return cur;
        cur = cur->next;
    }
    return NULL;
}



/**
 * @brief  Genera una Request di elebaorazione e la inserisce nella requests_list
 * @note   
 * @param  type:char il tipo di richiesta
 * @param  *ds:time_t puntatore alla data di inizio ( NULL per default data minima )
 * @param  *de:time_t puntatore alla data di fine ( NULL per dafault data massima )
 * @param  *socket:int puntatore alla socket del richiedente 
 *         ( NULL se siamo noi a generare la richiesta )
 * @param  *id:int puntatore all'id del richiedente
 *         ( NULL se siamo noi a generare la richiesta )
 * @retval puntatore alla struttura request generata
 */
struct request* init_request(char type,time_t *ds,time_t *de,int *socket,int *id){
    struct request *req;
    req = malloc(sizeof(struct request));
    if(req == NULL) return NULL;
    time(&req->timestamp);
    if(ds>de){
        printf("La data di inizio è più grande di quella di fine!\n");
        return NULL;
    }
    req->date_start=(ds==NULL)?0:*ds;
    req->date_end=(de==NULL)?0x7fffffffffffffff:*de;
    req->flooding_list = init_flooding_list();
    req->next = NULL;
    req->ret_socket = (socket==NULL)?-1:*socket;//Se NULL sono io che faccio la richiesta
    req->request_type = type;
    req->id = (id==NULL)?my_id:*id;
    requestes_list_add(req);
    return req;
}

struct entry {
    //Identifica la entry
    struct timespec timestamp;
    char type;
    long quantity;
    struct entry *next;
};

struct entries_register{
    time_t creation_date;
    FILE *f;
    char *path;
    int is_open;
    unsigned int count;
    struct entry* entries;
    struct entries_register *next;
    
};

struct entries_register *registers_list;

/**
 * @brief  Compara due entry
 * @note   
 * @param  x:entry*   
 * @param  y:entry* 
 * @retval 0: uguali , 1: x>y -1:x<y
 */
int entriescmp(struct entry* x,struct entry* y){
    if(x->timestamp.tv_sec<y->timestamp.tv_sec) return -1;
    if(x->timestamp.tv_sec>y->timestamp.tv_sec) return 1;
    if(x->timestamp.tv_nsec<y->timestamp.tv_nsec) return -1;
    if(x->timestamp.tv_nsec>y->timestamp.tv_nsec) return 1;
    return 0; //sono uguali
}

struct entry* entry_init(char type,long quantity){
    struct entry* e;
    if(type!='n' && type!='t') return NULL; //type non valido
    if(quantity == 0) return NULL; //Entry vuota!
    e = malloc(sizeof(struct entry));
    if( e == NULL) return NULL;
    timespec_get(&e->timestamp,TIME_UTC);
    //Aggiungo le due ore del nostro fuso orario (in secondi)
    e->timestamp.tv_sec+=7200;
    e->quantity = quantity;
    e->type = type;
    e->next = NULL;
    return e;
}

/**
 * @brief  Inserisce il record nella lista del registro
 * @note   
 * @param  *er:entries_register il registro nel quale inserire il record
 * @param  *e:entry il record
 * @retval None
 */
int add_entry_in_register(struct entries_register *er,struct entry *e){
    struct entry* cur;
    struct entry* prev;
    int ret;
    cur = er->entries;
    prev = NULL;
    while(cur){
        ret = entriescmp(cur,e);
        if(ret == 0) return 0;//entry duplicata
        if(ret == 1) break;//Siamo arrivati al punto di inserimento
        prev = cur;
        cur = cur->next;
    }
    if(prev==NULL){
        printf("Nessuno in lista lo aggiungo alla testa %s\n",ctime(&e->timestamp.tv_sec));
        er->entries = e;
    }else{
        printf("Lo aggiungo dopo a %s -> %s\n",ctime(&prev->timestamp.tv_sec),ctime(&e->timestamp.tv_sec));
        prev->next = e;
    }
    e->next = cur;
    er->count++;
    return 1;
}

/**
 * @brief  Restituisce il Register aperto ( può essere aperto solo un register per volta )
 * @note   
 * @retval entries_register* puntatore al registro, NULL se non esiste. 
 */
struct entries_register* get_open_register(){
    struct entries_register* cur;
    cur = registers_list;
    while(cur){
        if(cur->is_open) return cur;
        else cur = cur->next;
    }
    // Nessun registro aperto!
    return NULL;
}
/**
 * @brief  Carica il register da file
 * @note   
 * @param  f: 
 * @retval 
 */
/*struct entries_register* load_register(FILE f){
    
}
void save_register(){

}*/

/**
 * @brief  Cerca la prima lista aperta della registers_list e 
 *         ci inserisce il record passato, aggiorna anche il relativo file.
 * @note   vedi add_entry_in_register , get_open_register
 * @param  *e:entry il record da inserire
 * @retval int 0 errore 1 ok
 */
int add_entry(struct entry *e){
    int ret;
    pthread_mutex_lock(&register_mutex);
    struct entries_register *er = get_open_register();
    if(er==NULL){
        pthread_mutex_unlock(&register_mutex);
        return 0;
    }
    ret =  add_entry_in_register(er,e);
    if(ret){
        er->f = fopen(er->path,"a");
        fprintf(er->f,"%ld.%ld:%c,%ld\n",e->timestamp.tv_sec,
        e->timestamp.tv_nsec,
        e->type,
        e->quantity);
        fclose(er->f);
    }
    pthread_mutex_unlock(&register_mutex);
    return ret;
}
/**
 * @brief  Crea una entry e la inserisce nella entries_register aperta
 * @note   add_entry(struct entry *e)
 * @param  type:char tipo di entry ( t:tampone n:nuovo caso ) 
 * @param  quantity:long la quantità di type
 * @retval 
 */
int create_entry(char type, unsigned int quantity){
    struct entry *e =  malloc(sizeof(struct entry));
    e->next = NULL;
    e->quantity = quantity;
    e->type = type;
    timespec_get(&e->timestamp,TIME_UTC);
    e->timestamp.tv_sec+= 7200; //Noi siamo avanti 2 ore rispetto UTC
    return add_entry(e);
}

int registers_list_add(struct entries_register *er){
    struct entries_register *cur;
    struct entries_register *prev;
    cur = registers_list;
    prev = NULL;
    while(cur){
        //È un duplicato, non aggiungiamo nulla
        if(cur->creation_date==er->creation_date)return 0;
        //Siamo arrivati al punto di inserimento
        if(cur->creation_date>er->creation_date)break;
        prev = cur;
        cur = cur->next;
    }
    if(prev==NULL){
        registers_list = er;
    }else{
        prev->next = er;
    }
    er->next = cur;
    return 1;
}

void registers_list_free(){
    struct entries_register *cur;
    struct entries_register *prev;
    struct entry *e_cur;
    struct entry *e_prev;
    cur = registers_list;
    while(cur){
        prev = cur;
        cur = cur->next;
        e_cur = prev->entries;
        while(e_cur){
            e_prev = e_cur;
            e_cur = e_cur->next;
            free(e_prev);
        }
        free(prev);
    }
}

void registers_list_print(){
    struct entries_register *cur;
    struct entry *e_cur;
    cur = registers_list;
    while(cur){
        e_cur = cur->entries;
        printf("register: %s",ctime(&cur->creation_date));
        while(e_cur){
            printf("entry: %s|->type:%c quantity: %ld\n",ctime(&e_cur->timestamp.tv_sec),e_cur->type,e_cur->quantity);  
            e_cur = e_cur->next;
        }
        cur = cur->next;
    }
    printf("fine stampa\n");
}

/**
 * @brief  Stampa la lista dei vicini
 * @note   
 * @retval None
 */
void neighbors_list_print(){
    struct neighbour*cur;
    pthread_mutex_lock(&neighbors_list_mutex);
    printf("Neighbors list:\n");
    cur = neighbors_list;
    while(cur){
        printf("[id:%d,s:%d,p:%d]\n",cur->id,cur->socket,ntohs(cur->addr.sin_port));
        cur = cur->next;
    }
    pthread_mutex_unlock(&neighbors_list_mutex);
}

int send_greeting_message(struct neighbour* n);
struct neighbour* neighbour_init_and_connect(int id,struct in_addr addr,int port){
    struct neighbour *n = malloc(sizeof(struct neighbour));
    n->id = id;
    n->next = NULL;
    n->socket = socket(AF_INET,SOCK_STREAM,0);
    memset(&n->addr,0,sizeof(n->addr)); //Pulizia
    n->addr.sin_family = AF_INET; //Tipo di socket
    n->addr.sin_port = htons(port);//Porta
    n->addr.sin_addr = addr;
    if(connect(n->socket,(struct sockaddr*)&n->addr,sizeof(n->addr))==0){
        if(send_greeting_message(n)) return n;
        perror("Errore nell'invio del messaggio di saluto!\n");
        return NULL;
    }else{
        perror("Errore nell'apertura della neighbour socket");
        return NULL;
    }
}

/**
 * @brief  Inizializza un nuovo vicino quando questo si presenta al peer con un greeting_message
 * @note   Vedi send_greeting_message
 * @param  id:int l'identificativo del peer
 * @param  socket:int la socket del peer
 * @param  addr:in_addr l'indirizzo del peer 
 * @param  s_port:int la porta del peer 
 * @retval  neighbour* puntatore al nuovo vicino allocato
 */
struct neighbour* neighbour_init(int id,int socket,struct in_addr addr,int s_port){
    struct neighbour *n = malloc(sizeof(struct neighbour));
    n->id = id;
    n->next = NULL;
    n->socket = socket;
    memset(&n->addr,0,sizeof(n->addr)); //Pulizia
    n->addr.sin_family = AF_INET; //Tipo di socket
    n->addr.sin_port = htons(s_port);//Porta
    n->addr.sin_addr = addr;
    return n;
}


/**
 * @brief  Inserimento in coda di un nuovo vicino nella neighbors_list
 * @note   
 * @param  n:neighbour* puntatore al vicino da inserire 
 * @retval None
 */
void neighbors_list_add(struct neighbour* n){
    struct neighbour*cur,*prev;
    cur = neighbors_list;
    prev = NULL;
    while(cur){
        prev = cur;
        cur = cur->next;
    }
    if(prev==NULL){
        neighbors_list = n;
    }else{
        prev->next = n;
    }
    neighbors_number++;
}
/**
 * @brief  Rimuove il vicino dalla lista restituendolo, NULL se non esiste
 * @note   Non dealloca il vicino
 * @param  id:int l'id del vicino da rimuove dalla lista
 * @retval struct neighbour* puntatore al vicino rimosso
 */
struct neighbour* neighbors_list_remove(int id){
    struct neighbour*cur,*prev=NULL;
    struct neighbour* ret = NULL;
    cur = neighbors_list;
    while(cur){
        if(cur->id == id){
            break;
        }
        prev = cur;
        cur = cur->next;
    }
    ret = cur;
    if(cur && prev){
        prev->next = cur->next;
        neighbors_number--;
    }else if(prev==NULL && cur!=NULL){
        neighbors_list = cur->next;
        neighbors_number--;
    }
    return ret;
}

/**
 * @brief Restituisce un vicino dato l'id se presente nella neighbors_list
 * @note   
 * @param  id:int l'ID del vicino richiesto 
 * @retval neighbour* puntatore al vicino richiesto, NULL se non è presente
 */
struct neighbour* get_neighbour_by_id(int id){
    struct neighbour*cur;
    cur = neighbors_list;
    while(cur){
        if(cur->id == id){
            return cur;
        }
        cur = cur->next;
    }
    return NULL;
}

/**
 * @brief  Alloca un nuovo elemento neighbour e lo inserisce nella neighbors_list,
 * se la socket è stata passata come parametro l'aggiunge al fd_set master, altrimenti 
 * apre anche una connessione con il vicino.
 * @note   
 * @param  id:int id del vicino 
 * @param  socket:int socket del vicino che richiede la connessione, -1 se siamo noi a connetterci al peer 
 * @param  addr:in__addr indirizzo del vicino
 * @param  port:int porta del vicino 
 * @retval struct neighbour* elemento che rappresenta il vicino
 */
struct neighbour* add_neighbour(int id,int socket,struct in_addr addr,int port) {
    struct neighbour* n;   
    pthread_mutex_lock(&neighbors_list_mutex);
    n = get_neighbour_by_id(id);
    if(n!=NULL){
        pthread_mutex_unlock(&neighbors_list_mutex);
        return n;//Già presente!
    } 
    if(socket==-1){//Ricevuto dal DS
        printf("socket -1 creo connessione\n");
        n = neighbour_init_and_connect(id,addr,port);
    }else{//Ricevuto dal socket listener
        printf("socket %d aggiungo vicino\n",socket);
        n = neighbour_init(id,socket,addr,port);
    }
    if(n==NULL){
        pthread_mutex_unlock(&neighbors_list_mutex);
        return NULL;
    }
    //Bisogna inserire la nuova socket nel master e aggiornare la socket più grande.
    pthread_mutex_lock(&fd_mutex);
    FD_SET(n->socket,&master);
    max_socket = (n->socket>max_socket) ?n->socket:max_socket;
    pthread_mutex_unlock(&fd_mutex);
    //E infine inserisco il nuovo vicino nella lista dei vicini
    neighbors_list_add(n);
    printf("Tutto fatto\n");
    pthread_mutex_unlock(&neighbors_list_mutex);
    neighbors_list_print();
    return n;
}

char* get_file_name(char*path){
    char *file_name;
    int s_start = strlen(path);
    int s_end = strlen(path);
    char c ;
    do{
        s_start--;
        c = path[s_start];
    }while(c!='/');
    s_start++;
    do{
        s_end--;
        c = path[s_end];
    }while(c!='.');
    file_name = malloc(sizeof(char)*(s_end-s_start));
    strncpy(file_name,path+s_start,s_end-s_start);
    return file_name;
}
/**
 * @brief  Apre il registro della data corrente, se è già stato chiuso
 *         apre quello del giorno successivo e così via.
 * @note   
 * @retval None
 */
struct entries_register* open_today_register(){
    struct entries_register *er;
    int ret;
    char filename[20];
    er = malloc(sizeof(struct entries_register));
    time_t today;
    struct tm *timeinfo;
    time(&today);
    //printf("%s",ctime(&today));
    timeinfo = localtime(&today);
    timeinfo->tm_sec = 0;
    timeinfo->tm_min = 0;
    timeinfo->tm_hour = 0;
    today = mktime(timeinfo);
    //printf("%s",ctime(&today));
    er->count=0;
    er->entries = NULL;
    er->is_open = 1;
    er->next = NULL;
    //printf("%s",ctime(&today));
    do{
        er->creation_date = today;
        ret = registers_list_add(er);
        today+=86400; //passo al giorno successivo
    //La data di oggi potrebbe essere già stata chiusa
    }while(!ret);
    //Completiamo l'inizializzazione
    sprintf(filename,"%d-%ld",er->is_open,er->creation_date);
    er->path = malloc(sizeof(char)*(strlen(my_path)+strlen(filename)+7));//+7 perché considero anche '/' e "\0" e .txt
    strcpy(er->path,my_path);
    strcat(er->path,"/");
    strcat(er->path,filename);
    strcat(er->path,".txt");
    return er;
}



/**
 * @brief  Chiude il registro di oggi e il relativo file cambiandogli nome
 * @note   
 * @retval None
 */
void close_today_register_file(){
    struct entries_register *er;
    char *old_path;
    int position;
    
    er = get_open_register();
    if(!er){//Tutti i registri sono chiusi
        printf("Tutti i registri sono chiusi!\n");
        return; 
    }
    position = strlen(er->path);
    old_path = malloc(sizeof(char)*(strlen(er->path)+1));
    strcpy(old_path,er->path);
    er->is_open = 0;
    fclose(er->f);
    //Cambio nome al file per contrassegnarlo come chiuso
    while(er->path[position]!='-') position--;
    er->path[position-1] = '0';
    rename(old_path,er->path);
}

/**
 * @brief  Chiude il registro aperto e apre quello del giorno successivo.
 * @note   
 * @retval None
 */
void close_today_register(){
    struct entries_register *er;
    pthread_mutex_lock(&register_mutex);
close_loop:
    close_today_register_file();
    er = open_today_register();
    //Mi sincronizzo con gli altri peer
    if(current_open_date > er->creation_date) goto close_loop;
    pthread_mutex_unlock(&register_mutex);
}

/**
 * @brief  Setta la data di sincronizzazione
 * @note   Utilizzata dal thread che comunica con il DS
 * @param  new_open_date:time_t la nuova data ricevuta 
 * @retval None
 */
void set_current_open_date(time_t new_open_date){
    pthread_mutex_lock(&register_mutex);
    current_open_date = new_open_date;
    pthread_mutex_unlock(&register_mutex);
}

time_t get_current_open_date(){
    time_t ret;
    pthread_mutex_lock(&register_mutex);
    ret =  current_open_date;
    pthread_mutex_unlock(&register_mutex);
    return ret;
}

/**
 * @brief  Carica il registro dal file
 * @note   formato <secondi>.<nano>:<tipo>,<quantità>
 * @param  path:char* stringa del path 
 * @retval None
 */
struct entries_register* load_register(char* path){
    struct entries_register *er;
    struct entry *e;
    struct entry aux;
    memset(&aux,0,sizeof(struct entry));
    er = malloc(sizeof(struct entries_register));
    char* file_name = get_file_name(path);
    printf("il path è %s\n il nome del file è %s\n",path,file_name);
    sscanf(file_name,"%d-%ld",&er->is_open,&er->creation_date);
    er->path = malloc(sizeof(char)*(strlen(path)+1));
    strcpy(er->path,path);
    er->entries = NULL;
    er->next = NULL;
    er->count = 0;
    //if(path==NULL) return;
    er->f = fopen(path,"r");
    while(fscanf(
        er->f,
        "%ld.%ld:%c,%ld",
        &aux.timestamp.tv_sec,
        &aux.timestamp.tv_nsec,
        &aux.type,
        &aux.quantity)!=EOF){
        printf("Dentro %ld.%ld:%c,%ld\n",
        aux.timestamp.tv_sec,
        aux.timestamp.tv_nsec,
        aux.type,
        aux.quantity);
        e = malloc(sizeof(struct entry));
        e->timestamp.tv_sec = aux.timestamp.tv_sec;
        e->timestamp.tv_nsec =aux.timestamp.tv_nsec;
        e->type = aux.type;
        e->quantity= aux.quantity;
        e->next = NULL;
        add_entry_in_register(er,e);
    }
    registers_list_add(er);
    fclose(er->f);
    printf("fine load\n");
    return er;
}

int update_register_by_remote_string(char* buffer){
    struct entries_register *er;
    struct entry *e;
    int size;
    int index=0;
    int tot_size = strlen(buffer);
    pthread_mutex_lock(&register_mutex);
    er = get_open_register();
    if(er==NULL){
        //Nessun registro aperto
        pthread_mutex_unlock(&register_mutex);
        return 0;
    }
    do{
        size = 0;
        do{//Considero anche lo /n
            size++;
        }while(buffer[index+size]!='\n');
        e = malloc(sizeof(struct entry));
        sscanf(
        buffer+index,
        "%ld.%ld:%c,%ld",
        &e->timestamp.tv_sec,
        &e->timestamp.tv_nsec,
        &e->type,
        &e->quantity);
        printf("Dentro %ld.%ld:%c,%ld\n",
        e->timestamp.tv_sec,
        e->timestamp.tv_nsec,
        e->type,
        e->quantity);
        add_entry_in_register(er,e);
        index = size+1;
    }while(tot_size>index);
    pthread_mutex_unlock(&register_mutex); 
    return 1;
}

/**
 * @brief  prove
 * @note   prova note
 * @param  socket:int la Socket del vicino richiesto
 * @retval struct neighbour* che rappresenta il vicino, NULL se non esiste.
 */
struct neighbour* get_neighbour_by_socket(int socket){
    struct neighbour*cur;
    cur = neighbors_list;
    while(cur){
        if(cur->socket == socket){
            return cur;
        }
        cur = cur->next;
    }
    return NULL;
}

/**
 * @brief  Elimina il vicino dalla neighbors_list e dal fd_set master.
 * @note   
 * @param  id:int l'identificativo del vicino da eliminare
 * @retval 0 operazione fallita, 1 operazione riuscita
 */
int remove_neighbour(int id){
    pthread_mutex_lock(&neighbors_list_mutex);
    struct neighbour *n = neighbors_list_remove(id);
    if(n==NULL){
        pthread_mutex_unlock(&neighbors_list_mutex);
        return 0; //Non esiste nesssun vicino con quell'id
    } 
    pthread_mutex_lock(&fd_mutex);
    FD_CLR(n->socket,&master);
    pthread_mutex_unlock(&fd_mutex);
    free(n);
    pthread_mutex_unlock(&neighbors_list_mutex);
    neighbors_list_print();
    return 1;
}

/**
 * @brief  Dealloca tutti gli elementi presenti nella neighbors_list
 * @note   
 * @retval None
 */
void neighbors_list_free(){
    struct neighbour*cur,*prev;
    cur = neighbors_list;
    while(cur){
        prev = cur;
        cur = cur->next;
        remove_neighbour(prev->id);
    }
}




/*
### DS REQUESTER THREAD ########################
*/
struct sockaddr ds_addr;



/*
### NEIGHBORS MANAGER THREAD ##################
*/

/*void* neighbors_loop(int s_port){
    int ret,newfd,listener,addrlen,i,len,fdmax;

}*/




/*
### GLOBALS INIT E FREE #####################
*/

void generate_work_folders(){
    char path[50];
    int result;
    sprintf(path,"./%d",my_port);
    printf("%s\n",path);
    result = mkdir(path, 0777);
    my_path = malloc(sizeof(char)*(strlen(path)+1));
    strcpy(my_path,path);
    if(result == -1){
        printf("Cartella %s già presente\n",my_path);
    }else{
        printf("Cartella %s creata\n",my_path);
    }
    sprintf(path+strlen(path),"/log");
    result = mkdir(path, 0777);
    my_log_path = malloc(sizeof(char)*(strlen(path)+1));
    strcpy(my_log_path,path);
    if(result == -1){
        printf("Cartella %s già presente\n",my_log_path);
    }else{
        printf("Cartella %s creata\n",my_log_path);
    }
}

void registers_list_init(){
    DIR *d;
    struct dirent *dir;
    struct entries_register *er;
    char *path_n_file_name;
    d = opendir(my_path);
    if (d) {
        while ((dir = readdir(d)) != NULL) {
            if(strcmp((dir->d_name+strlen(dir->d_name)-3),"txt")==0){
                printf("%s estensione %s\n", dir->d_name,(dir->d_name+strlen(dir->d_name)-3));
                path_n_file_name = malloc(sizeof(char)*(strlen(my_path)+strlen(dir->d_name)+2));//+2 perché considero anche '/' e "\n"
                strcpy(path_n_file_name,my_path);
                strcat(path_n_file_name,"/");
                strcat(path_n_file_name,dir->d_name);
                load_register(path_n_file_name);
                free(path_n_file_name);
            }
    }
    closedir(d);
  }
  er = get_open_register();
  if(er==NULL){er = open_today_register();}
  current_open_date = er->creation_date;
}


void ds_option_set(char c);
/**
 * @brief  Inizializza tutte le variabili globali condivise dai thread
 * @note   
 * @retval None
 */
void globals_init(){
    my_id =-1;
    ds_option_set('x');
    inet_pton(AF_INET,LOCAL_HOST,&my_addr);
    max_socket = 0;
    neighbors_number = 0;
    loop_flag = 1;
    ds_comunication_thread = 0;
    tcp_comunication_thread = 0;
    FD_ZERO(&master);
    FD_ZERO(&to_read);
    neighbors_list = NULL;
    generate_work_folders();
    registers_list_init();
    pthread_mutex_init(&neighbors_list_mutex,NULL);
    pthread_mutex_init(&register_mutex,NULL);
    pthread_mutex_init(&fd_mutex,NULL);
    pthread_mutex_init(&ds_mutex,NULL);
}

/**
 * @brief  Richiama tutte le funzioni per deallocare le strutture dati globali
 * @note   
 * @retval None
 */
void globals_free(){
    neighbors_list_free();
}
/*
### USER INTERFACE ############################
*/
/**
 * @brief  Controlla se il comando passato esiste
 * @note   
 * @param  command:char* stringa del commando 
 * @retval index:int indice del comando, -1 se il comando non esiste
 */
int find_command(char* command){
    char command_list[][PEER_MAX_COMMAND_SIZE] = PEER_COMMAND_LIST;
    for(int i = 0; i<PEER_COMMANDS_NUMBER; i++){
        if(strcmp(command,command_list[i]) == 0 ) return i;
    }
    return -1;
}







/*
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
    ds_addr.sin_s_port = htons(ds_port);//Porta
    inet_pton(AF_INET,LOCAL_HOST,&ds_addr.sin_addr);
    ds_addrlen = sizeof(ds_addr);
    sendto(socket,msg,5,0,(struct sockaddr*)&ds_addr,ds_addrlen);
    close(socket);
}
*/
void send_backups_to_all();
void* ds_comunication_loop(void*arg);
void send_byebye_to_all();
/**
 * @brief  Loop con il quale l'utente interagisce con il programma
 * @note   
 * @retval None
 */
void user_loop(){
    void* ret;
    char confirm;
    char msg[40];
    int port;
    int args_number;
    int command_index;
    char args[3][PEER_MAX_COMMAND_SIZE];
    char type;
    long quantity;
    printf(PEER_WELCOME_MSG);
    while(loop_flag){
        printf(">> ");
        fgets(msg, 40, stdin);
        args_number = sscanf(msg,"%s %s %s",args[0],args[1],args[2]);
        //arg_len = my_parser(&args,msg);
        if(args_number>0){
            command_index = find_command(args[0]);
        }else{
            command_index = -1;
        }
        switch(command_index){//Gestione dei comandi riconosciuti
            //__help__
            case 0:
                printf(PEER_HELP_MSG);
                break;
            //__start__
            case 1:
                if(args_number < 2){
                    printf("Manca la porta del DS\n");
                    break;
                }
                port = atoi(args[1]);
                if(pthread_create(&ds_comunication_thread,NULL,ds_comunication_loop,(void*)&port)){
                    perror("Errore nella creazione del thread\n");
                    exit(EXIT_FAILURE);
                }
                sleep(2);
                break;
            //__add__
            case 2:
                type = *args[1];
                if( type!='n' && type!='t'){
                    printf("Tipo non valido, accettato solo n e t\n");
                    break;
                }
                for(int i =0;i<strlen(args[2]);i++){
                    if(args[2][i]=='-') args[2][i]=' ';
                }
                quantity = strtol(args[2],NULL,0);
                if(quantity==0){
                    printf("Inserire una quantità maggiore o uguale di 0!\n");
                }
                if(type =='t'){
                    printf("Aggiungere %ld nuovi tamponi?<y/n>\n>> ",quantity);
                }else{
                    printf("Aggiungere %ld nuovi positivi?<y/n>\n>> ",quantity);
                }
                
                scanf("%c",&confirm);
                //Rimuovo lo /n lasciata dalla scanf
                while((getchar()) != '\n'); 
                if(confirm!='y'){
                    printf("Entry annullata\n");
                    break;
                }
                if(create_entry(type,quantity)==0){
                    printf("Impossibile inserire una nuova entry!\n");
                    break;
                }
            break;
            //__get__
            case 3:
                printf("to do\n");
                
            break;
            //__esc__
            case 4:
                printf("Chiusura in corso...\n");
                ds_option_set('b');
                pthread_join(ds_comunication_thread,&ret);
                send_backups_to_all();
                sleep(1);
                send_byebye_to_all();
                loop_flag = 0;
                pthread_join(tcp_comunication_thread,&ret);
                printf("Socket chiusa\n");
                
            break;
            //__comando non riconosciuto__
            default:
                printf("Comando non riconosciuto!\n");
            break;
        }

    }


}


/*void send_exit_packet(int ds_port){
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
}*/

/*
    uint64_t prova=0;
    uint64_t mask = 0x00000000FFFFFFFF;
    uint32_t len = 598764;
    uint32_t option = 'n'; //4 byte option;
    option = htonl(option);
    len = htonl(len);
    prova = option;
    prova = prova<<32 | len;
    printf("%u,%u\n",prova>>32,prova & mask);
    option = prova>>32;
    len = prova & mask;
    option = htonl(option);
    len = htonl(len);
    printf("%u,%u\n",option,len);
    return 1;
*/
/**
 * @brief  Genera il messaggio di saluto;
 * @note   
 * @param  msg:char**  stringa ( passata per riferimento ) sulla quale viene allocato il messaggio
 * @retval unsigned int contenente la lunghezza del messaggio
 */
unsigned int generate_greeting_message(char** msg){
    char buffer[255];
    unsigned int len;
    sprintf(buffer,"%d,%u,%d",my_id,my_addr.s_addr,my_port);
    len = sizeof(char)*strlen(buffer)+1;
    *msg = malloc(len);
    strcpy(*msg,buffer);
    return len;
}

/**
 * @brief  Funziona per farsi aggiungere alla neighbors_list del peer a cui si sta mandando il messaggio
 * @note   vedi generate_greeting_message
 * @param  n:neighbour* il peer a cui mandare il messaggio
 * @retval 0 operazione fallita, 1 operazione eseguita con successo
 */
int send_greeting_message(struct neighbour* n){
    char *buffer = NULL;
    uint64_t first_packet=0; // contiene la lunghezza del prossimo messaggio e del codice
    uint32_t len; // lunghezza messaggio
    uint32_t operations=0;
    char operation = 'H';//Hello!
    if(n == NULL) return 0; //Non dovrebbe MAI accadere
    /*GENERO IL MESSAGGIO E LO MANDO DOPO AVER MANDATO IL PRIMO PACCHETTO*/
    len = generate_greeting_message(&buffer);
    printf("Il messaggio è lungo %u ed è %s\n",len,buffer);
    len = htonl(len);
    operations = (unsigned int)operation;
    operations = htonl(operations);
    first_packet = operations;
    first_packet = first_packet<<32 | len;
    len = ntohl(len);
    printf("First packet %lu, ed è lungo %lu\n",first_packet,sizeof(first_packet));
    //Mando il primo pacchetto con l'operazione richiesta
    //e la lunghezza del secondo pacchetto
    
    if(send(n->socket,(void*)&first_packet,sizeof(first_packet),0)<0){
        perror("Errore in fase di invio\n");
        return 0;
    }
    if(send(n->socket,(void*)buffer,(size_t)len,0)<0){//size_t è un unsigned long
        perror("Errore in fase di invio\n");
        return 0;
    }
    printf("Saluto inviato!\n");
    return 1;
}
/**
 * @brief  Genera una stringa contentente tutti i dati di un registro
 * @note   Formato messaggio
 *         Nome registro
 *         entries
 * 
 * @param  *er:entries_register puntatore al registro
 * @param  **msg:char Puntatore alla stringa 
 * @retval 
 */
int generate_entries_list_msg(struct entries_register *er,char **msg){
    int size;
    char name[64];
    sprintf(name,"%ld\n",er->creation_date);
    if(er == NULL) return 0;
    er->f=fopen(er->path,"r");
    fseek(er->f, 0L, SEEK_SET);
    fseek(er->f, 0L, SEEK_END);
    size = ftell(er->f);
    fseek(er->f, 0L, SEEK_SET);
    *msg = malloc(sizeof(char)*(size+strlen(name)));
    
    //memset(*msg,'\0',size+strlen(name));
    
    strcpy(*msg,name);
    if(size == 0){
        printf("Niente da copiare\n");
        free(*msg);
        msg = NULL;
        return size;
    }
    fread((void*)(*msg+strlen(name)),sizeof(char),size,er->f);
    (*msg)[size+strlen(name)]='\0';
    printf("\nStampo come stringa\n%s\n",*msg);
    fclose(er->f);
    return strlen(*msg);
}

/**
 * @brief  Inivia un messaggio al peer indicando che sta per abbandonare la rete
 * @note   
 * @param  n:neighbour* il peer a cui inviare il messaggio
 * @retval 0 operazione fallita, 1 operazione eseguita con successo
 */
int send_byebye_message(struct neighbour* n){
    uint64_t first_packet=0; // contiene la lunghezza del prossimo messaggio e del codice
    uint32_t len; // lunghezza messaggio
    uint32_t operations=0;
    char operation = 'B';//Bye Bye!
    if(n == NULL) return 0; //Non dovrebbe MAI accadere
    /*GENERO IL MESSAGGIO E LO MANDO DOPO AVER MANDATO IL PRIMO PACCHETTO*/
    len = 0;// generate_entries_list_msg(get_open_register(),&entry_list_msg); //Non mandiamo nessun secondo messaggio
    len = htonl(len);
    operations = (unsigned int)operation;
    operations = htonl(operations);
    first_packet = operations;
    first_packet = first_packet<<32 | len;
    len = ntohl(len);
    //Mando il primo pacchetto con l'operazione richiesta
    //e la lunghezza del secondo pacchetto  
    if(send(n->socket,(void*)&first_packet,sizeof(first_packet),0)<0){
        perror("Errore in fase di invio\n");
        return 0;
    }
    printf("Addio inviato a %d\n",n->id);
    return 1;
}

/*
⢀⣠⣾⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⠀⠀⠀⠀⣠⣤⣶⣶
⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⠀⠀⠀⢰⣿⣿⣿⣿
⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⣧⣀⣀⣾⣿⣿⣿⣿
⣿⣿⣿⣿⣿⡏⠉⠛⢿⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⡿⣿
⣿⣿⣿⣿⣿⣿⠀⠀⠀⠈⠛⢿⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⠿⠛⠉⠁⠀⣿
⣿⣿⣿⣿⣿⣿⣧⡀⠀⠀⠀⠀⠙⠿⠿⠿⠻⠿⠿⠟⠿⠛⠉⠀⠀⠀⠀⠀⣸⣿
⣿⣿⣿⣿⣿⣿⣿⣷⣄⠀⡀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⢀⣴⣿⣿
⣿⣿⣿⣿⣿⣿⣿⣿⣿⠏⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠠⣴⣿⣿⣿⣿ I LIMONI SIGNORAAAAA
⣿⣿⣿⣿⣿⣿⣿⣿⡟⠀⠀⢰⣹⡆⠀⠀⠀⠀⠀⠀⣭⣷⠀⠀⠀⠸⣿⣿⣿⣿
⣿⣿⣿⣿⣿⣿⣿⣿⠃⠀⠀⠈⠉⠀⠀⠤⠄⠀⠀⠀⠉⠁⠀⠀⠀⠀⢿⣿⣿⣿
⣿⣿⣿⣿⣿⣿⣿⣿⢾⣿⣷⠀⠀⠀⠀⡠⠤⢄⠀⠀⠀⠠⣿⣿⣷⠀⢸⣿⣿⣿
⣿⣿⣿⣿⣿⣿⣿⣿⡀⠉⠀⠀⠀⠀⠀⢄⠀⢀⠀⠀⠀⠀⠉⠉⠁⠀⠀⣿⣿⣿
⣿⣿⣿⣿⣿⣿⣿⣿⣧⠀⠀⠀⠀⠀⠀⠀⠈⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⢹⣿⣿
⣿⣿⣿⣿⣿⣿⣿⣿⣿⠃⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⢸⣿⣿
*/

/**
 * @brief  Lancia send_byebye_message(...) a tutti i membri della neighbors_list
 * @note   Vedi send_byebye_message
 * @retval None
 */
void send_byebye_to_all(){
    struct neighbour* n;
    pthread_mutex_lock(&neighbors_list_mutex);
    n = neighbors_list;
    while(n){
        send_byebye_message(n);
        n = n->next;
    }
    pthread_mutex_unlock(&neighbors_list_mutex);
}

/**
 * @brief  Genera una stringa contenente le entry suddivise per registro
 * @note   
 * @param  *start:time_t data dalla quale iniziare il backup ( NULL se si desidera generarla dall'inizio) 
 * @param  *end:time_t ultima data prima di completare il backup ( NULL se le si desiderano tutte fino alla fine)
 * @retval 
 */
char* generate_backup_message(time_t *s,time_t *e){
    int size;
    char name[64];
    char *msg=NULL;
    char *tot_msg = NULL;
    struct entries_register *er;
    time_t start,end;
    er = registers_list;
    if(er == NULL) return NULL;
    end = (e==NULL)?0x7fffffffffffffff:*e;
    start = (s==NULL)?0:*s;
    printf("%s",ctime(&end));
    while(er){
        if(er->creation_date<start){//Data non compresa
            er = er->next;
            continue;
        }
        if(er->creation_date>end)break;//Abbiamo superato la data di fine
        sprintf(name,"%ld,%u\n",er->creation_date,er->count);
        er->f=fopen(er->path,"r");
        fseek(er->f, 0L, SEEK_SET);
        fseek(er->f, 0L, SEEK_END);
        size = ftell(er->f);
        fseek(er->f, 0L, SEEK_SET);
        msg = malloc(sizeof(char)*(size+strlen(name)));
        strcpy(msg,name);
        if(size == 0){//File vuoto
            free(msg);
            msg = NULL;
            er = er->next;
            continue;
        }
        fread((void*)(msg+strlen(name)),sizeof(char),size,er->f);
        msg[size+strlen(name)]='\0';
        if(tot_msg == NULL){
            tot_msg = malloc(strlen(msg)+1);
            strcpy(tot_msg,msg);
            
        }else{
            tot_msg = realloc(tot_msg,(strlen(tot_msg)+strlen(msg)+1));
            //tot_msg[strlen(tot_msg)] = '\n';
            strncat(tot_msg,msg,strlen(msg));
        }
        fclose(er->f);
        er = er->next;
        
    }
    return tot_msg;
}
/**
 * @brief  Invia tutte le entries a tutti i vicini
 * @note   thread safe per neighbors_list e registers_list
 * @retval None
 */
void send_backups_to_all(){
    //struct neighbour *n;
    uint64_t first_packet=0; // contiene la lunghezza del prossimo messaggio e del codice
    uint32_t len; // lunghezza messaggio
    uint32_t operations=0;
    struct neighbour *n;
    pthread_mutex_lock(&neighbors_list_mutex);
    char operation = 'U';//Update!
    char *msg = generate_backup_message(NULL,NULL);
    n = neighbors_list;
    if(n==NULL){
        printf("Nessun vicino!\n");
        goto end_send_to_all;
        return;
    }
    if(msg==NULL){
        printf("Niente da inviare\n");
        goto end_send_to_all;
        return;
    }
    len = strlen(msg);
    len = htonl(len);
    operations = (unsigned int)operation;
    operations = htonl(operations);
    first_packet = operations;
    first_packet = first_packet<<32 | len;
    len = ntohl(len);
    while(n){
        printf("Generato il messaggio di lunghezza %dl\n messaggio:\n%s\n",len,msg);
    //Mando il primo pacchetto con l'operazione richiesta
    //e la lunghezza del secondo pacchetto  
        if(send(n->socket,(void*)&first_packet,sizeof(first_packet),0)<0){
            perror("Errore in fase di invio");
            n=n->next;
            continue;
        }
        printf("Inviato il primo messaggio\n");
        if(send(n->socket,(void*)msg,len+1,0)<0){//+1 per carattere terminatore
            perror("Errore in fase di invio");
            n=n->next;
            continue;
        }
        printf("Backup inviato a %d porta=%u\n",n->id,n->addr.sin_port);
        n=n->next;
        printf("Fine loop\n");
    }
    
end_send_to_all:
    pthread_mutex_unlock(&neighbors_list_mutex);
    printf("Fine invio backup!\n");
}

/**
 * @brief  Genera il messaggio di richiesta da inviare
 * @note   
 * @param  *r:request la request dalla quale generare il messaggio 
 * @retval char* puntatore alla stringa
 */
char* generate_request_message(struct request *r){
    char* msg;
    char aux[128];
    if(r==NULL)return NULL;
    sprintf(aux,"%d,%ld,%ld,%ld,%c\n",r->id,r->timestamp,r->date_start,r->date_end,r->request_type);
    msg = malloc(strlen(aux));
    strcpy(msg,aux);
    return msg;
}

/**
 * @brief  Invia la richiesta a tutti i peer
 * @note   
 * @param  *r:request la richiesta 
 * @param  *avoid_socket:int la socket alla quale abbiamo ricevuto 
 *  la richiesta che stiamo inoltrando, NULL se siamo noi a generare 
 *  la prima richiesta e inviarla così a tutte le socket
 * 
 * @retval None
 */
void send_request_to_all(struct request *r,int *avoid_socket){
    //struct neighbour *n;
    uint64_t first_packet=0; // contiene la lunghezza del prossimo messaggio e del codice
    uint32_t len; // lunghezza messaggio
    uint32_t operations=0;
    struct neighbour *n;
    pthread_mutex_lock(&neighbors_list_mutex);
    char operation = 'R';//Request!
    char *msg = generate_request_message(r);
    n = neighbors_list;
    if(n==NULL){
        printf("Nessun vicino!\n");
        goto end_req_to_all;
        return;
    }
    if(msg==NULL){
        printf("Niente da inviare\n");
        goto end_req_to_all;
        return;
    }
    len = strlen(msg);
    len = htonl(len);
    operations = (unsigned int)operation;
    operations = htonl(operations);
    first_packet = operations;
    first_packet = first_packet<<32 | len;
    len = ntohl(len);
    printf("Generato il messaggio di lunghezza %dl\n messaggio:\n%s\n",len,msg);
    while(n){
        if(avoid_socket){//Socket da evitare ( ci ha inviato lei la richiesta )
            if(n->socket==*avoid_socket){
                n = n->next;
                continue;
            }
        }        
    //Mando il primo pacchetto con l'operazione richiesta
    //e la lunghezza del secondo pacchetto  
        if(send(n->socket,(void*)&first_packet,sizeof(first_packet),0)<0){
            perror("Errore in fase di invio");
            n=n->next;
            continue;
        }
        printf("Inviato il primo messaggio\n");
        if(send(n->socket,(void*)msg,len+1,0)<0){//+1 per carattere terminatore
            perror("Errore in fase di invio");
            n=n->next;
            continue;
        }
        printf("Backup inviato a %d porta=%u\n",n->id,n->addr.sin_port);
        n=n->next;
        printf("Fine loop\n");
    }
    
end_req_to_all:
    pthread_mutex_unlock(&neighbors_list_mutex);
    if(msg) free(msg);
    printf("Fine invio backup!\n");
}
/**
 * @brief  Invia i dati richiesti da una socket alla ret_socket
 * @note   
 * @param  *r: 
 * @retval None
 */
void send_request_data(struct request *r){
    char *backup;
    char *req_header;
    char *msg;
    uint64_t first_packet=0; // contiene la lunghezza del prossimo messaggio e del codice
    uint32_t len; // lunghezza messaggio
    uint32_t operations=0;    char operation = 'A';//Answer!
    if(r==NULL){
        printf("La request è NULL!\n");
        return;
    }
    backup = generate_backup_message(&r->date_start,&r->date_end);
    req_header = generate_request_message(r);
    if(!backup || !req_header){
        printf("Nienten da inviare\n");
        return;
    }
    msg =malloc(strlen(backup)+strlen(req_header)+1);
    strcpy(msg,req_header);
    strcat(msg,backup);
    len = strlen(msg);
    len = htonl(len);
    operations = (unsigned int)operation;
    operations = htonl(operations);
    first_packet = operations;
    first_packet = first_packet<<32 | len;
    len = ntohl(len);
    printf("Generato il messaggio di lunghezza %dl\n messaggio:\n%s\n",len,msg);
    //Mando il primo pacchetto con l'operazione richiesta
    //e la lunghezza del secondo pacchetto  
        if(send(r->ret_socket,(void*)&first_packet,sizeof(first_packet),0)<0){
            perror("Errore in fase di invio");
            goto req_end;
        }
        printf("Inviato il primo messaggio\n");
        if(send(r->ret_socket,(void*)msg,len+1,0)<0){//+1 per carattere terminatore
            perror("Errore in fase di invio");
            goto req_end;
        }
req_end:
        free(backup);
        free(req_header);
        free(msg);
}

int manage_register_backup(char *msg);
/**
 * @brief  Gestisce la risposta aggiornando i registri e inviando la sua risposta se ha completato la sua porzione di request
 * @note   Operation A
 * @param  *buffer:char il messaggio di risposta 
 * @param  socket:int dalla quale ho ricevuto una risposta 
 * @retval 
 */
int manage_request_answer(char *buffer,int socket){
    unsigned long index=0;
    struct neighbour *n;
    struct request *r;
    int id;
    char t;//type
    time_t s,e,tp;//start,end,timestamp
    char aux[128];
    sscanf(buffer,"%s",aux);
    index+=strlen(aux)+1;
    sscanf(aux,"%d,%ld,%ld,%ld,%c",&id,&tp,&s,&e,&t);
    r = requestes_list_get(id,tp);
    if(r==NULL){//Non dovrebbe accadere, ma la gestiamo
        printf("Risposta di una richiesta inesistente\n");
        return 0;
    }
    n = get_neighbour_by_socket(socket);
    if(!flooding_list_check_flooding_mate(r->flooding_list,n->id,socket)){
        printf("Il vicino %d non è in lista!\n",n->id);
        /*Qualcosa è andato storto, abbiamo ricevuto un pacchetto
        * non aspettato. Lo ignoriamo.
        */
        return 0;
    }
    manage_register_backup(buffer+index);
    if(flooding_list_all_checked(r->flooding_list)){
        if(r->ret_socket==-1){//Sono io che ho iniziato la richiesta
            //TO DO
            printf("FATTO!\n");
        }else{//Invio il risultato alla ret_socket di r
            send_request_data(r);
        }
    }
    return 1;

}
/**
 * @brief  Invio un messaggio alla socket di non aspettare la mia risposta alla sua richiesta perché
 *         la sto aspettando anche io e stiamo generando un loop
 * @note   Vedi manage_new_request
 * @param  *msg:char puntatore alla stringa che descrive la request
 * @param  socket:int socket a cui inviare il messaggio 
 * @retval None
 */
int send_ignore_my_answer(char *msg,int socket){
    uint64_t first_packet=0; // contiene la lunghezza del prossimo messaggio e del codice
    uint32_t len; // lunghezza messaggio
    uint32_t operations=0;
    char operation = 'I';//Ignore!
    len = strlen(msg);
    len = htonl(len);
    operations = (unsigned int)operation;
    operations = htonl(operations);
    first_packet = operations;
    first_packet = first_packet<<32 | len;
    len = ntohl(len);
    if(send(socket,(void*)&first_packet,sizeof(first_packet),0)<0){
            perror("Errore in fase di invio");
            return 0;
        }
        printf("Inviato il primo messaggio\n");
        if(send(socket,(void*)msg,len+1,0)<0){//+1 per carattere terminatore
            perror("Errore in fase di invio");
            return 0;
    }
    return 1;
}

/**
 * @brief  Gestisce le nuove richieste in entrata dai vicini
 * @note   
 * @param  *buffer:char i dati della richiesta 
 * @param  socket:int la socket dalla quale abbiamo ricevuto la richiesta 
 * @retval int 0 già presente, 1 nuova richiesta inserita con successo
 */
int manage_new_request(char *buffer,int socket){
    struct request *r;
    int id;
    char t;//type
    time_t s,e,tp;//start,end,timestamp  
    sscanf(buffer,"%d,%ld,%ld,%ld,%c",&id,&tp,&s,&e,&t);
    r = requestes_list_get(tp,id);
    if(r){
        printf("Richiesta già presente, gestisco il loop\n");
        /* Invio una risposta una risposta speciale
        *  per togliermi dai flooding mates.
        */
        if(!send_ignore_my_answer(buffer,socket)){
            printf("Impossibile inviare la richiesta di ignoramento alla socket=%d\n",socket);
        }
        return 0;
    }
    //Una nuova richiesta!
    r = init_request(t,&s,&e,&socket,&id);
    //Propago la richiesta!
    send_request_to_all(r,&socket);
    return 1;
}
/**
 * @brief  Gestisce il messaggio "ignora" ricevuto da una socket
 * @note   
 * @param  *buffer:char messaggio contente la request
 * @param  socket:int la socket da ignorare 
 * @retval 
 */
int manage_ignore_answer(char* buffer, int socket){
    struct request *r;
    int id;
    char t;//type
    time_t s,e,tp;//start,end,timestamp  
    sscanf(buffer,"%d,%ld,%ld,%ld,%c",&id,&tp,&s,&e,&t);
    r = requestes_list_get(tp,id);
    if(r == NULL){
        /*Qualcosa è andato storto ho ricevuto il messaggio
        * di ignorare una risposta a una richiesta che non possiedo*/
       return 0;
    }
    //Lo flaggo e ignoro
    flooding_list_check_flooding_mate(r->flooding_list,socket,id);
    return 1;
}

struct entries_register* get_register_by_creation_date(time_t cd){
    struct entries_register* er = registers_list;
    while(er){
        if(er->creation_date==cd) return er;
        er = er->next;
    }
    return NULL;
}

/**
 * @brief  Aggiunge le entry che sono arrivate tramite un messaggio di backup
 * @note   
 * @param  msg:char* il messaggio ricevuto
 * @retval 1 almeno un inserimento, 0 nessun inserimento
 */
int manage_register_backup(char *msg){
    time_t cd;
    struct entries_register *er;
    struct entry *e;
    size_t len = 0;
    int final_ret = 0;
    int ret=0;
    int loop_size;
    char aux[100];
    printf("Il messaggio\n%s\n",msg);
    pthread_mutex_lock(&register_mutex);
    while(sscanf(msg+len,"%s",aux)==1){
        len += strlen(aux)+1;
        sscanf(aux,"%ld,%u",&cd,&loop_size);
        er = get_register_by_creation_date(cd);
        for(int i=0;i<loop_size;i++){
            sscanf(msg+len,"%s",aux);
            len+=strlen(aux)+1;
            e = malloc(sizeof(struct entry));
            printf("Leggo riga %s\n",aux);
            if(sscanf(aux,"%ld.%ld:%c,%ld",
            &e->timestamp.tv_sec,
            &e->timestamp.tv_nsec,
            &e->type,
            &e->quantity)==4){
                if(er==NULL){
                    printf("er è NULL\n");
                    continue;
                }
                ret = add_entry_in_register(er,e);
                if(ret){//È stato effettivamento inserito ( non è un duplicato )
                    //Controlliamo che il registro non sia chiuso
                    final_ret = final_ret || ret;
                    er->f=fopen(er->path,"a");
                    fprintf(er->f,"%ld.%ld:%c,%ld\n",e->timestamp.tv_sec,
                    e->timestamp.tv_nsec,
                    e->type,
                    e->quantity);
                    //Infine richiudiamo il registro
                    fclose(er->f);
                }
            }else{//messaggio corrotto
                continue;
            }
        }
    }
    pthread_mutex_unlock(&register_mutex);
    return final_ret;
}

/*
DS formato messaggio
<id>,<numero vicini>
<id_vicino>,<indirizzo>,<s_porta>
... (ripetuto per <numero vicini0>)
<id_vicino>,<indirizzo>,<s_porta>
*/


/**
 * @brief  
 * @note   
 * @param  msg:char* stringa contenente  
 * @retval None
 */
void update_neighbors(char* msg){
    char aux[100];
    struct in_addr neig_addr;
    int id,s_port;
    id = s_port = -1;
    unsigned long index = 0;
    int entries_number;
    sscanf(msg,"%s\n",aux);
    sscanf(aux,"%d,%d",&my_id,&entries_number);
    printf("Il mio id = %d\n",my_id);
    index = strlen(aux)+1;
    for(int i = 0; i<entries_number;i++){
        printf("msg:\n%s",msg+index);
        sscanf(msg+index,"%s\n",aux);
        printf("Leggo riga %s\n",aux);
        sscanf(aux,"%d,%u,%d",&id,&neig_addr.s_addr,&s_port);
        //-1 perché non conosciamo la socket
        add_neighbour(id,-1,neig_addr,s_port);
        //+1 perché devo considerare anche \n
        index += strlen(aux)+1;
    }
}


//Indica il tipo di servezion richiesto al DS
char ds_option;

/**
 * @brief  Restituisce il valore di ds_option che specifica il servizio richiesto al DS
 * @note   vedi ds_comunication_loop
 * @retval char contentenente l'opzione per quella iterazione
 */
char ds_option_get(){
    char c;
    pthread_mutex_lock(&ds_mutex);
    c = ds_option;
    pthread_mutex_unlock(&ds_mutex);
    return c;
}
/**
 * @brief  Setta ds_option che specifica il servizio da richiedere al DS alla prossima iterazione del ds_comunication_loop
 * @note   vedi ds_comunicazion_llop
 * @param  c:char tipo di opzione 
 * @retval None
 */
void ds_option_set(char c){
    pthread_mutex_lock(&ds_mutex);
    ds_option = c;
    pthread_mutex_unlock(&ds_mutex);
}
/**
 * @brief  Gestisce la comunicazione con il DS ottentendo i vicini
 *         e sincronizzando la data del registro aperto. 
 * @note   
 *           formato messaggio ricevuto richiesta vicini
 *           <id>,<numero vicini>
 *           <id_vicino>,<indirizzo>,<s_porta>
 *           ... (ripetuto per <numero vicini>)
 *           <id_vicino>,<indirizzo>,<s_porta>
 *           
 *          
 * @param  *arg:void contiene la porta del DS
 * @retval None
 */
void* ds_comunication_loop(void *arg){
    fd_set ds_master;
    fd_set ds_to_read;
    int ret;
    time_t time_recived=10;
    struct timeval timeout;
    char buffer[DS_BUFFER];
    int socket,s_port,ds_port;
    socklen_t ds_addrlen;
    char option;
    struct sockaddr_in addr,ds_addr;
    inet_pton(AF_INET,LOCAL_HOST,&addr.sin_addr);
    s_port = MIN_PORT;
    //Apro una socket udp per riceve i messaggi da DS
    while(s_port<MAX_PORT){
        //Continuo a ciclare finché non trovo una porta libera oppure
        //Finisco le porte
        ret = open_udp_socket(&socket,&addr,s_port);
        if(ret < 0){
            if(s_port<MAX_PORT){
                s_port++;
                continue;
            }else{
                perror("Impossibile connettersi al Discovery Server");
                pthread_exit(NULL);
            }
        }
        printf("Socket udp aperta alla porta %d\n",s_port);
        break;
    }
    FD_ZERO(&ds_master);
    FD_ZERO(&ds_to_read);
    FD_SET(socket,&ds_master);
    
    timeout.tv_usec = 0;
    //Costruisco l'indirizzo al DS
    ds_port = *(int*)arg;
    ds_addr.sin_family = AF_INET; //Tipo di socket
    ds_addr.sin_port = htons(ds_port);//Porta
    inet_pton(AF_INET,LOCAL_HOST,&ds_addr.sin_addr);
    ds_addrlen = sizeof(ds_addr);
    
    while(loop_flag){
        option = ds_option_get();
        //Richiesta vicini
        sprintf(buffer,"%u,%d,%ld,%c",my_addr.s_addr,my_port,current_open_date,option);
  
        sendto(socket,&buffer,strlen(buffer)+1,0,(struct sockaddr*)&ds_addr,ds_addrlen);
        ds_to_read = ds_master;
        timeout.tv_sec = DS_COMUNICATION_LOOP_SLEEP_TIME;
        ret = select(socket+1,&ds_to_read,NULL,NULL,&timeout); //Abbiamo solo una socket nella lista
        //perror("Select:");
        if(ret==1){
            if(recvfrom(socket,buffer,DS_BUFFER,0,(struct sockaddr*)&ds_addr,&ds_addrlen)<0){
                printf("Errore nella ricezione");
                continue;
            }
            if(option=='x'){//Richiesta della lista dei vicini
                printf("messaggio ricevuto:\n %s",buffer);
                update_neighbors(buffer);
                ds_option_set('r');//Il loop passa in modalità sincronizzazione ( refresh )
            }else if(option == 'r'){
                printf("Ricevuto \n%s\n",buffer);
                printf("Subito dopo ricevuto\n%ld",time_recived);
                sscanf(buffer,"%ld\n",&time_recived);
                printf("Il mio tempo:%ld , ricevuto:%ld ",get_current_open_date(),time_recived);
                if(time_recived > get_current_open_date()){//dobbiamo sincronizzarci
                    set_current_open_date(time_recived);
                    close_today_register();
                }
            }
        }
        if(option == 'b'){//byebye
            break;
        }
    }   
    close(socket);
    pthread_exit(NULL);
}

/**
 * @brief  Gestisce le richieste dei vicini
 * @note   vedi tcp_comunication _loop
 * @param  socket_served:int la socket che richiede un servizio al peer
 * @retval None
 */
void serve_peer(int socket_served){
    char *buffer;
    uint64_t first_packet=0; // contiene la lunghezza del prossimo messaggio e del codice
    uint32_t len; // lunghezza messaggio
    uint32_t options;// 4 byte contenente una opzione per byte
    char operation; // Corrisponde al primo byte di options, 
                    // indica il tipo di operazione  da svolgere con 
                    // il prossimo pacchetto
    int peer_id,peer_port;
    struct in_addr peer_addr;
    //printf("Servo %d",socket_served);
    if(recv(socket_served,(void*)&first_packet,sizeof(first_packet),0)<0){
        remove_neighbour(get_neighbour_by_socket(socket_served)->id);
        ds_option_set('x');//Richiedo di nuovo la neighbour list al DS, magari ho un nuovo amichetto
        return;
    }
    //4 byte di opzioni e 4byte che indica la lunghezza del pacchetto
    options = first_packet>>32;
    len = first_packet & PACKET_MASK;
    options = ntohl(options);
    len = ntohl(len);
    //printf("L'operazione è %d e la lunghezza è %d\n",options,len);
    operation =(char) options; // ci serve il primo byte
    //printf("operation %c\n",operation);
    printf("Giunta operazione %c con lungezza %d\n",operation,len);
    if(len>0){
        buffer = malloc(sizeof(char)*len);
            if(recv(socket_served,(void*)buffer,len,0)<0){
                perror("Errore in fase di ricezione\n");
                return;
            }
    }
    switch(operation){
        case 'H': //Nuovo peer (Hello!)
            sscanf(buffer,"%d,%u,%d",&peer_id,&peer_addr.s_addr,&peer_port);
            //printf("Quello che ho letto %d,%u,%d\n",peer_id,peer_addr.s_addr,peer_port);
            add_neighbour(peer_id,socket_served,peer_addr,peer_port);
            break;
        case 'U'://Update
            printf("Ricevuto un messaggio di backup da %d\n",socket_served);
            printf("Messaggio ricevuto\n %s\n",buffer);
            manage_register_backup(buffer);
            printf("Lista aggiornata\n");
            registers_list_print();
            break;
        case 'R'://Request
            printf("Nuova Request da %d\n",socket_served);
            printf("Messaggio ricevuto\n %s\n",buffer);
            manage_new_request(buffer,socket_served);
            break;
        case 'I'://Ignore
            printf("Ricevuto 'Ignora' da %d\n",socket_served);
            printf("Messaggio ricevuto\n %s\n",buffer);
            manage_ignore_answer(buffer,socket_served);
            break;
        case 'A'://Answer
            printf("Ricevuto una risposta da %d\n",socket_served);
            printf("Messaggio ricevuto\n %s\n",buffer);
            manage_request_answer(buffer,socket_served);
            break;
        case 'B'://bye bye
            remove_neighbour(get_neighbour_by_socket(socket_served)->id);
            ds_option_set('x');//Richiedo di nuovo la neighbour list al DS, magari ho un nuovo vicino
            break;
        default:
            //Il messaggio non rispetta i protocolli di comunicazione, rimuovo il peer
            printf("Il peer non rispetta i protocolli di comunicazione, lo elimino\n");
            remove_neighbour(get_neighbour_by_socket(socket_served)->id);
            ds_option_set('x');//Richiedo di nuovo la neighbour list al DS, magari ho un nuovo vicino
            break;
    }
    
}

void * tcp_comunication_loop(void *arg){
    int peer_socket;
    struct sockaddr_in s_addr;
    struct sockaddr_in peer_addr;
    int s_port = *((int*)arg);
    int ret;
    socklen_t len;
    struct timeval timeout;
    timeout.tv_usec = 0;
    ret = open_tcp_server_socket(&s_socket,&s_addr,s_port);
    if(ret<0){
        perror("Errore nella costruzione della server socket\n");
        exit(-1);
    }
    ret = listen(s_socket,DEFAULT_SOCKET_SLOTS);
    printf("Socket tcp aperta alla porta: %d\n",s_port);
    
    FD_SET(s_socket,&master);
    printf("Server socket = %d\n",s_socket);
    pthread_mutex_lock(&fd_mutex);
    max_socket = s_socket;
    pthread_mutex_unlock(&fd_mutex);
    while(loop_flag){
        pthread_mutex_lock(&fd_mutex);
        to_read = master;
        pthread_mutex_unlock(&fd_mutex);
        timeout.tv_sec = DEFAULT_SELECT_WAIT_TIME; 
        ret = select(max_socket+1,&to_read,NULL,NULL,&timeout);
        for(int socket_i = 0; socket_i<=max_socket;socket_i++){
            if(FD_ISSET(socket_i,&to_read)){//Socket ready to read;
                if(socket_i == s_socket){
                    len = sizeof(peer_addr);
                    peer_socket = accept(s_socket,(struct sockaddr*) &peer_addr,&len);
                    printf("È arrivato un nuovo peer : %d\n",peer_socket);
                    serve_peer(peer_socket);
                }else{//Serviamo un peer;
                    printf("Servo il client : %d\n",socket_i);
                    serve_peer(socket_i);
                    
                    /*if(send(socket_i,(void*)buffer,len,0)<0){
                        perror("Errore in fase di invio\n");
                    }*/
                    

                }
            }
        }
    }
    pthread_exit(NULL);
}

/*
uint64_t prova=0;
uint64_t mask = 0x00000000FFFFFFFF;
char ugo = 'n';
prova = ugo;
prova = prova<<32 | (unsigned int)2560 ;
printf("%c,%u",prova>>32,prova & mask);

*/

/*
    uint64_t prova=0;
    uint64_t mask = 0x00000000FFFFFFFF;
    uint32_t len = 598764;
    uint32_t option = 'n'; //4 byte option;
    option = htonl(option);
    len = htonl(len);
    prova = option;
    prova = prova<<32 | len;
    printf("%u,%u\n",prova>>32,prova & mask);
    option = prova>>32;
    len = prova & mask;
    option = htonl(option);
    len = htonl(len);
    printf("%u,%u\n",option,len);
    return 1;
*/



int main(int argc, char* argv[]){
    if(argc>1){
        my_port = atoi(argv[1]);
    }else{
        my_port = DEFAULT_PORT;
    }
    globals_init();
    registers_list_print();
    if(pthread_create(&tcp_comunication_thread,NULL,tcp_comunication_loop,(void*)&my_port)){
        perror("Errore nella creazione del thread\n");
        exit(EXIT_FAILURE);
    }
    sleep(1);
    printf("inizio user loop\n");
    user_loop();
    globals_free();
    printf("Ciao, ciao!\n");
}
