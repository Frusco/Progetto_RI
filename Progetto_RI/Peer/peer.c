
#include "../consts/socket_consts.h"
#include "../consts/peer_consts.h"
#include "../libs/my_sockets.h"
#include "../libs/my_logger.h"
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
//log del thread di comunicazione con gli altri peer
struct my_log *peer_log;
//Log del thread di comunicazione con il DS
struct my_log *ds_log;
//Log del thread utente
struct my_log *user_log;
//Data del registro attualmente aperto
time_t current_open_date;
//FD set contenente tutte le socket
fd_set master;
//FD set contenente le socket da leggere
fd_set to_read;
//Mutex per garantire mutualità esclusiva agli fd_set (durante l'inserimento o la rimozione di un nuovo peer)
pthread_mutex_t fd_mutex;
//Mutex per garantire mutualità esclusiva al char contentente l'opzione da richiede al DS (r: refresh TTL x: get Neighbors)
//E per il loop_flag
pthread_mutex_t set_get_mutex;
pthread_mutex_t register_mutex;
pthread_mutex_t request_mutex;
pthread_cond_t request_busy;
int req_busy_flag;
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
    //Indica se il registro è aperto
    int is_open;
    //Indica se il registro è completo
    int is_completed;
    unsigned int count;
    struct entry* entries;
    struct entries_register *next;
    
};

struct entries_register *registers_list;

struct request{
    //Insieme a timestamp identifica la richiesta
    int id;
    //Insieme a id identifica la richiesta
    time_t timestamp;
    //La socket alla quale inviare il risultato completato il flooding;
    int ret_socket;
    //Tipo entry
    char entry_type;
    //Tipo richiesta
    char req_type;
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


struct FILE_request{
    //La richiesta
    struct request* r;
    //Lista dei vicini a cui abbiamo inviato la richiesta del file
    struct flooding_mate *fm;
    //Il nome del file;
    char* file_name;
};
//Puntatore alla richiesta FILE corrente
struct FILE_request *cur_FILE_request;
struct flooding_mate* init_flooding_list(int avoid_socket);

/**
 * @brief  Blocca il thread utente in attesa di una risposta della Richiesta effettuata
 * @note   Vedi user_loop()
 * @retval None
 */
void wait_request_elaboration(){
    pthread_mutex_lock(&request_mutex);
    while(req_busy_flag) pthread_cond_wait(&request_busy,&request_mutex);
    pthread_mutex_unlock(&request_mutex);
}
/**
 * @brief  Blocca l'input utente per la durata dell'elaborazione della richiesta
 * @note   Vedi
 * @retval None
 */
void lock_user_input(){
    pthread_mutex_lock(&request_mutex);
    req_busy_flag=1;
    pthread_mutex_unlock(&request_mutex);
}
/**
 * @brief  Sblocca l'input utente a Richiesta elaborata
 * @note   Vedi 
 * @retval None
 */
void unlock_user_input(){
    pthread_mutex_lock(&request_mutex);
    req_busy_flag=0;
    pthread_cond_broadcast(&request_busy);
    pthread_mutex_unlock(&request_mutex);
}

/**
 * @brief  Setta il file richiesto, può esserci solo una richiesta di file alla volta
 * @note   
 * @param  r:request* richiesta della quale cerchiamo l'elaborato già completato 
 * @retval 1 file request settata, 0 già presente una file request 
 */
int cur_FILE_request_set(struct request* r){
    char aux[128];
    int len;
    if(cur_FILE_request) return 0;
    cur_FILE_request = malloc(sizeof(struct FILE_request));
    //Copio la request per iniziare il flooding in caso di fallimento di ricevere il file
    cur_FILE_request->r = r;
    //Costruisco il nome del file che cerchiamo
    len = sprintf(aux,"/%c%c-%ld_%ld.res",r->req_type,r->entry_type,r->date_start,r->date_end);
    aux[len]='\0';
    cur_FILE_request->file_name = malloc(len+1);
    strcpy(cur_FILE_request->file_name,aux);
    cur_FILE_request->fm = init_flooding_list(-1);
    return 1;
}
/**
 * @brief  Dealloca le var dinamiche della struttura FILE_request
 * @note   
 * @retval None
 */
void cur_FILE_request_free(){
    struct flooding_mate *aux;
    if(cur_FILE_request==NULL)return;
    if(cur_FILE_request->file_name!=NULL){
        free(cur_FILE_request->file_name);
    }
    
    while(cur_FILE_request->fm){
        aux = cur_FILE_request->fm;
        cur_FILE_request->fm = cur_FILE_request->fm->next;
        free(aux);
    }
    free(cur_FILE_request);
    cur_FILE_request = NULL;
}
/**
 * @brief  Invia ai vicini la richiesta del file contenuta in cur_FILE_request
 * @note   
 * @retval None
 */
void send_FILE_request(){
    uint64_t first_packet=0; // contiene la lunghezza del prossimo messaggio e del codice
    uint32_t len=0; // lunghezza messaggio
    uint32_t operations=0;
    struct neighbour* n;
    n = neighbors_list;
    if(n==NULL){
        printf("Nessun vicino!\n");
        return;
    }
    char operation = 'r';//r:FILE request
    len = strlen(cur_FILE_request->file_name)+1;
    //printf("filename=%s len=%u",cur_FILE_request->file_name,len);
    len = htonl(len);
    operations = (unsigned int)operation;
    operations = htonl(operations);
    first_packet = operations;
    first_packet = first_packet<<32 | len;
    len = ntohl(len);
    my_log_print(user_log,"FILE richiesto: %s\n",cur_FILE_request->file_name);
    while(n){
        if(send(n->socket,(void*)&first_packet,sizeof(first_packet),0)<0){
                perror("Errore in fase di invio");
                return;
            }
        if(send(n->socket,(void*)cur_FILE_request->file_name,len,0)<0){//+1 per carattere terminatore
            perror("Errore in fase di invio");
            return;
        }
        n = n->next;
    }
}

/**
 * @brief  Invia il contenuto del file, se il file non esiste invece invia una riposta NOT FOUND
 * @note   vedi manage_FILE_request
 * @param  socket_served:int la socket alla quale inviare la risposta 
 * @param  msg:char* stringa del contenuto del file, può essere NULL per indicare File not found
 * @retval None
 */
void send_FILE_answer(char* msg,int socket_served){
    uint64_t first_packet=0; // contiene la lunghezza del prossimo messaggio e del codice
    uint32_t len; // lunghezza messaggio
    uint32_t operations=0;
    char operation = (msg==NULL)?'N':'F';//N:not found F:File
    len = (msg==NULL)?0:htonl(strlen(msg)+1);
    operations = (unsigned int)operation;
    operations = htonl(operations);
    first_packet = operations;
    first_packet = first_packet<<32 | len;
    len = ntohl(len);
    if(send(socket_served,(void*)&first_packet,sizeof(first_packet),0)<0){
            perror("Errore in fase di invio");
            return;
        }
    if(msg!=NULL){
        if(send(socket_served,(void*)msg,len,0)<0){//+1 per carattere terminatore
            perror("Errore in fase di invio");
            return;
        }
    }
}
/**
 * @brief  Gestisce la richiesta del file voluto
 * @note   vedi send_FILE_answer
 * @param  socket:int socket del richiedente 
 * @param  buffer:char* buffer che contiene il nome del file
 * @retval None
 */
void manage_FILE_request(char *buffer,int socket_served){
    char *path;
    char *msg;
    int size;
    int ret;
    FILE *f;
    path = malloc(strlen(my_path)+strlen(buffer)+1);
    strcpy(path,my_path);
    strcat(path,buffer);
    my_log_print(peer_log,"file da cercare:%s\n",path);
    f = fopen(path,"rb");
    if(!f){
        my_log_print(peer_log,"File NON trovato!\n");
        send_FILE_answer(NULL,socket_served);
        return;
    }
    fseek(f, 0L, SEEK_SET);
    fseek(f, 0L, SEEK_END);
    size = ftell(f);
    my_log_print(peer_log,"Trovato il file, dimensione: %dB\n",size);
    fseek(f, 0L, SEEK_SET);
    msg = malloc(size);
    if(!msg){
        my_log_print(peer_log,"Errore durante l'allocazione della memoria per leggere il FILE\n");
        send_FILE_answer(NULL,socket_served);
        return;
    }
    if(size==0){
        my_log_print(peer_log,"Trovato il file, ma è vuoto!\n");
        fclose(f);
        send_FILE_answer(NULL,socket_served);
        return;
    }
    my_log_print(peer_log,"Tentativo di lettura\n");
    ret = fread((void*)(msg),sizeof(char),size,f);
    if(ret!=size){
        my_log_print(peer_log,"Tentativo di lettura fallito size = %dB ma sono stati letti %dB\n",size,ret);
        fclose(f);
        send_FILE_answer(NULL,socket_served);
        return;
    }
    msg[size]='\0';
    fclose(f);
    my_log_print(peer_log,"Lettura completata con successo! Rispondo alla richiesta\n");
    send_FILE_answer(msg,socket_served);
}
int flooding_list_all_checked(struct flooding_mate *fm);
int flooding_list_check_flooding_mate(struct flooding_mate *fm,int id,int socket);
struct neighbour* get_neighbour_by_socket(int socket);
void send_request_to_all(struct request *r,int *avoid_socket);

/**
 * @brief  Gestisce la risposta del peer per FILE not found, se tutti i vicini hanno risposto in modo negativo, effettuo il flooding per ottenere i dati
 * @note   Abbiamo inviato la richiesta di un file, ma ci è giunta una risposta negativa
 * @param  socket_served:int la socket che stiamo servendo
 * @retval None
 */
void manage_FILE_not_found(int socket_served){
    struct neighbour *n;
    //Ci è giunta la risposta di una File_request che non esiste più, ignoro ed esco.
    if(cur_FILE_request==NULL)return;
    n = get_neighbour_by_socket(socket_served);
    flooding_list_check_flooding_mate(cur_FILE_request->fm,n->id,socket_served);
    if(flooding_list_all_checked(cur_FILE_request->fm)){
        printf("Nessun vicino possiede il FILE, effettuo un flooding per recupare i dati\n");
        send_request_to_all(cur_FILE_request->r,NULL);
        cur_FILE_request_free();
    }
}

/**
 * @brief  Gestisce la risposta del peer per FILE found, stampando il risultato a schermo
 * @note   
 * @param  buffer:char* la stringa contenente la risposta 
 * @param  socket_served:int la socket che stiamo servendo ( e che ci ha fornito la risposta ) 
 * @retval None
 */
void manage_FILE_found(char* buffer,int socket_served){
    struct neighbour *n;
    char* path;
    FILE *f;
    
    if(cur_FILE_request == NULL) return;//Abbiamo già ottenuto una risposta!
    n = get_neighbour_by_socket(socket_served);
    my_log_print(peer_log,"Il vicino id:%d socket:%d ha il file che cerchi\n",n->id,n->socket);
    path = malloc(strlen(my_path)+strlen(cur_FILE_request->file_name)+1);//+1
    my_log_print(peer_log,"my_path = %s len %ld\n",my_path,strlen(my_path));
    my_log_print(peer_log,"file_name = %s len %ld\n",cur_FILE_request->file_name,strlen(cur_FILE_request->file_name));
    strcpy(path,my_path);
    strcat(path,cur_FILE_request->file_name);
    my_log_print(peer_log,"Path = %s len %ld\n",path,sizeof(path));
    my_log_print(peer_log,"lunghezza buffer = %ld\n",strlen(buffer));
    my_log_print(peer_log,"Elaborato salvato in: %s\n",path);
    my_log_print(peer_log,"File_name = %s\n",cur_FILE_request->file_name+1);
    f = fopen(path,"wb");
    if(f==NULL){
        printf("Errore nella fopen con path= %s",path);
        unlock_user_input();
        return;
    }
    
    fwrite((void*)buffer,strlen(buffer),1,f);
    
    for(int i = 0;i<strlen(path)+1;i++)printf("%c",path[i]);
    printf("\nRisultato:\n%s\n",buffer);
    fclose(f);
    free(path);
    unlock_user_input();
    cur_FILE_request_free();
}






/**
 * @brief   Genera una lista di flooding_mate basata sui peer vicini
 * @note   Guarda neighbors_list , init_request
 * @retval 
 */
void flooding_list_print(struct flooding_mate* fm);
struct flooding_mate* init_flooding_list(int avoid_socket){
    struct flooding_mate *head;
    struct flooding_mate *f;
    struct flooding_mate *prev;
    struct neighbour *n;
    head = NULL;
    prev = NULL;
    pthread_mutex_lock(&neighbors_list_mutex);
    n = neighbors_list;
    while(n){
        if(n->socket == avoid_socket)goto skip_add;
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
    skip_add:
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
        fm = fm->next;
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
int flooding_list_check_flooding_mate(struct flooding_mate *fm,int id,int socket){
    while(fm){
        if(fm->socket==socket && fm->id==id){
            
            fm->recieved_flag = 1;
            return 1;
        }
        fm = fm->next;
    }
    return 0;
}

/**
 * @brief  Stampa la flooding list della lista passata
 * @note   
 * @param  fm:flooding_mate* lista dei flooding_mate 
 * @retval None
 */
void flooding_list_print(struct flooding_mate* fm){
    printf("Flooding list:\n");
    while(fm){
        printf("\t[id:%d,socket:%d,flag:%d]\n",fm->id,fm->socket,fm->recieved_flag);
        fm = fm->next;
    }
    printf("\n");
}


/**
 * @brief  Aggiunge la request alla requestes_list
 * @note   
 * @param  *e:request, la richiesta da inserire in lista 
 * @retval restituisce sempre 1
 */
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

/**
 * @brief  Aggiunge la request alla requestes_list
 * @note   
 * @param  *e:request, la richiesta da inserire in lista 
 * @retval restituisce sempre 1
 */
int requestes_list_remove(struct request *r){
    struct request *cur;
    struct request *prev;
    prev = NULL;
    cur = requestes_list;
    while(cur){
        if(cur->timestamp==r->timestamp  && cur->id == r->id){
            if(prev==NULL){
                requestes_list = cur->next;
            }else{      
                prev->next = cur->next;
            }
            free(cur);
            return 1;
        }
        prev = cur;
        cur = cur->next;
    }
    return 0;
}

/**
 * @brief  Restituisce la richiesta  identificandola per id del peer e data
 * @note   
 * @param  t:time_t la data della request
 * @param  id:int identificatore del peer che ha effettuato la richiesta 
 * @retval request* puntatore alla request, NULL se non esiste
 */
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
 * @brief  Dealloca la requestes_list
 * @note   
 * @retval None
 */
void requestes_list_free(){
    struct request *r;
    struct flooding_mate *fm;
    while(requestes_list){
        r = requestes_list;
        requestes_list = requestes_list->next;
        while(r->flooding_list){
            fm = r->flooding_list;
            r->flooding_list = r->flooding_list->next;
            free(fm);
        }
        free(r);
    }
}

/**
 * @brief  Data una lista di entry dello stesso tipo, elabora il totale
 * @note   Non effettua il controllo sul tipo ( type )
 * @param  *e:entry lista delle entry 
 * @retval char* stringa contentente il totale
 */
char* elab_totale(struct entry *e){
    char *msg;
    char aux[256];
    struct entry *tot;
    struct tm *start;
    struct tm *end;
    start = malloc(sizeof(struct tm));
    end = malloc(sizeof(struct tm));
    tot = malloc(sizeof(struct entry));
    memset(tot,0,sizeof(struct entry));
    tot->type = e->type;
    memcpy(start,localtime(&e->timestamp.tv_sec),sizeof(struct tm));
    while(e){
        tot->quantity += e->quantity;
        if(!e->next){
            memcpy(end,localtime(&e->timestamp.tv_sec),sizeof(struct tm));
        }
        e = e->next;
    }
    sprintf(aux,"Totale dal %d:%d:%d al %d:%d:%d è %ld\n",
    start->tm_year+1900,start->tm_mon+1,start->tm_mday,
    end->tm_year+1900,end->tm_mon+1,end->tm_mday,
    tot->quantity);
    msg = malloc(strlen(aux)+1);
    strcpy(msg,aux);
    free(tot);
    return msg;
}
void registers_list_print();
/**
 * @brief  Data una lista di entry dello stesso tipo, elabora il totale
 * @note   Non effettua il controllo sul tipo ( type )
 * @param  *e:entry lista delle entry 
 * @retval char* stringa contentente la variazione
 */
char* elab_variazione(struct entry *e){
    char *msg;
    char aux[1024];
    int i;
    struct entry *prev;
    struct tm *start;
    struct tm *end;
    long index = 0;
    prev = e;
    i = 1;
    start = malloc(sizeof(struct tm));
    end = malloc(sizeof(struct tm));
    while(prev){
        //printf("%ld, quantity%ld\n",prev->timestamp.tv_sec,prev->quantity);
        prev=prev->next;
    }
    prev = e;
    e = e->next;
    sprintf(aux,"Variazione (%c):\n",e->type);
    
    while(e){
        i++;
        index = strlen(aux);
        //Utilizzo memcpy perché localtime restituisce sempre il puntatore al suo buffer interno
        //che sovrascrive di volta in volta
        memcpy(end,localtime(&e->timestamp.tv_sec),sizeof(struct tm));
        memcpy(start,localtime(&prev->timestamp.tv_sec),sizeof(struct tm));
        sprintf(aux+index,"dal %d:%d:%d al %d:%d:%d è %ld\n",
        start->tm_year+1900,start->tm_mon+1,start->tm_mday,
        end->tm_year+1900,end->tm_mon+1,end->tm_mday,
        (e->quantity-prev->quantity));
        prev = e;
        e = e->next;
    }
    msg = malloc(strlen(aux)+1);
    strcpy(msg,aux);
    return msg;
}

struct entries_register* get_register_by_creation_date(time_t cd);
/**
 * @brief  Data una request, ne elabora il risultato come stringa
 * @note   vedi elab_variazione ed elab_totale
 * @param  *r:request puntatore alla richiesta
 * @retval char* stringa contentente il risultato
 */
char* get_elab_result_as_string(struct request *r){
    char * msg;
    struct entries_register *er;
    struct entry *e,*list,*list_prev,*head;
    my_log_print(peer_log,"Elaboro richiesta r_type-%c e_type-%c\n",r->req_type,r->entry_type);
    er = get_register_by_creation_date(r->date_start);
    if(er==NULL) er=registers_list;
    list_prev = NULL;
    head = NULL;
    do{
        list = malloc(sizeof(struct entry));
        memset(list,0,sizeof(struct entry));
        list->type = r->entry_type;
        list->quantity = 0;
        list->timestamp.tv_sec = er->creation_date;
        list->next = NULL;
        if(list_prev!=NULL){
            list_prev->next = list;
            list_prev = list;
        }
        if(head == NULL){ 
            head = list;
            list_prev = list;
        }
        e = er->entries;
        my_log_print(peer_log,"Inserisco le entries\n");
        while(e){
            my_log_print(peer_log,"Aggiungo %c %ld\n",e->type,e->quantity);
            if(e->type == list->type){ 
                list->quantity+=e->quantity;
            }else{
                my_log_print(peer_log,"Ignorato, tipo diverso\n");
            }
            
            e = e->next;
        }
        er = er->next;
    }while(er->creation_date<=r->date_end);
    
    switch(r->req_type){
        case 'v':
            my_log_print(peer_log,"Elaboro variazione richiesta\n");
            msg = elab_variazione(head);
        break;
        case 't':
            my_log_print(peer_log,"Elaboro totale richiesta\n");
            msg = elab_totale(head);
        break;
        default:
        my_log_print(peer_log,"Tipo di elaborazione non riconosciuta! %c\n",r->req_type);
        return NULL;
    }

    return msg;
}
/**
 * @brief  Controlla se i registri presenti che rientrano fra le date di inizio e fine
 * della request siano completi e chiusi
 * @note   vedi elab_request
 * @param  *r:request la richiesta
 * @retval int 1 : i registri sono tutti completi, 0 almeno un registro aperto o non completo, -1
 */
int registers_all_completed_and_closed(struct request *r){
    struct entries_register *er;
    //Non c'è nessun registro!
    if(registers_list == NULL) return 0;
    er = registers_list;
    while(er->creation_date<r->date_start) er = er->next;
    while(er->creation_date<=r->date_end){
            if(!er->is_completed)return 0;
            if(er->is_open)return 0;
            er = er->next;
    }
    return 1;
}

/**
 * @brief  Elabora la richiesta salvando il risultato su file
 * @note   
 * @param  *r:request richiesta da elaborare 
 * @retval char* path del file elaborato
 */
char* elab_request(struct request *r){
    FILE *f;
    char* path;
    char* result;
    char file_name[128];
    if(r==NULL)return NULL;
    my_log_print(peer_log,"Elaboro, richiesta...\n");
    //Variazione con un giorno solo?
    if(r->req_type=='v' && r->date_start==r->date_end)return NULL;
    //Controllo che tutti i peer coinvolti abbiano risposto
    //if(!flooding_list_all_checked(r->flooding_list))return NULL;
    //Controllo che tutti i registri coinvolti siano chiusi
    if(!registers_all_completed_and_closed(r)) return NULL;
    //Genero il path per il file
    sprintf(file_name,"/%c%c-%ld_%ld.res",r->req_type,r->entry_type,r->date_start,r->date_end);
    path = malloc(strlen(my_path)+strlen(file_name)+1);
    strcpy(path,my_path);
    strcat(path,file_name);
    my_log_print(peer_log,"Path: %s\n",path);
    f = fopen(path,"w");
    //Elaboro il risutlato
    result = get_elab_result_as_string(r);
    my_log_print(peer_log,"Risultato:\n%s\n",result);
    printf("Risultato:\n%s\n",result);
    fprintf(f,"%s",result);
    fclose(f);
    free(result);
    return path;
}

/**
 * @brief  Stampa le informazioni della richiesta
 * @note   
 * @retval None
 */
void requestes_list_print(){
    struct request *cur;
    cur = requestes_list;
    printf("Lista richieste:\n");
    while(cur){
        printf("req id:%d,socket:%d,r_type:%c,e_type:%c\n",cur->id,cur->ret_socket,cur->entry_type,cur->req_type);
        cur = cur->next;
    }
}

/**
 * @brief  Genera una Request di elebaorazione
 * @note   
 * @param  type:char il tipo di entry ( tampone o nuovi casi )
 * @param  req_type:char tipo di richiesta ( totale o variazione )
 * @param  *ds:time_t puntatore alla data di inizio ( NULL per default data minima )
 * @param  *de:time_t puntatore alla data di fine ( NULL per dafault data massima )
 * @param  *socket:int puntatore alla socket del richiedente 
 *         ( NULL se siamo noi a generare la richiesta )
 * @param  *id:int puntatore all'id del richiedente
 *         ( NULL se siamo noi a generare la richiesta )
 * @retval puntatore alla struttura request generata
 */
struct request* init_request(char type,char req_type,time_t *ds,time_t *de,int *socket,int *id){
    struct request *req;
    req = malloc(sizeof(struct request));
    if(req == NULL) return NULL;
    time(&req->timestamp);
    if(ds == NULL)goto fill_request_variables;
    if(*ds>*de){
        printf("La data di inizio è più grande di quella di fine!\n");
        printf("s:%s , e:%s\n",ctime(ds),ctime(de));
        return NULL;
    }
    //Controllo il vincolo del tipo di richiesta 'v' () variazione )
    if(req_type=='v'&& *ds==*de){
        printf("Non è possibile calcolare la variazione con una sola data!\n");
        return NULL;
    }
fill_request_variables:
    req->date_start=(ds==NULL)?0:*ds;
    req->date_end=(de==NULL)?0x7fffffffffffffff:*de;
    req->ret_socket = (socket==NULL)?-1:*socket;//Se NULL sono io che faccio la richiesta
    req->flooding_list = init_flooding_list(req->ret_socket);
    req->next = NULL;
    req->entry_type = type;
    req->req_type = req_type;
    req->id = (id==NULL)?my_id:*id;
    return req;
}


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
/**
 * @brief  Inizializza una entry con i parametri passati
 * @note   
 * @param  type:char typo di entry ( t: tampone , n: nuovo caso) 
 * @param  quantity:int quantità da registrare 
 * @retval entry* puntatore alla entry allocata
 */
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
        er->entries = e;
    }else{
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

void registers_list_print();
/**
 * @brief  Restituisce l'ultimo registro chiuso
 * @note   vedi add_new_request
 * @retval entries_register* puntatore al registro
 */
struct entries_register* get_last_closed_register(){
    struct entries_register* cur;
    struct entries_register* prev;
    cur = registers_list;
    prev = NULL;
    while(cur){
        if(cur->is_open){ return prev;}
        else {
            prev = cur;
            cur = cur->next;
        }
    }
    // Nessun registro aperto!
    return prev;
}
/**
 * @brief  Restituisce il registro chiuso più vecchio
 * @note   
 * @retval entries_register* puntatore all'elemento in testa alla registers_list se chiuso, NULL altrimenti
 */
struct entries_register* get_first_closed_register(){
    return (registers_list->is_open)?NULL:registers_list;
}

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
/**
 * @brief  Aggiunge una  un entries_register alla registers_list
 * @note   vedi manage_register_backup , load_register , open_today_register
 * @param  *er:entries_register puntatore al registro da inserire in lista 
 * @retval 
 */
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

/**
 * @brief  dealloca tutti i registri e relative entries
 * @note   
 * @retval None
 */
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
/**
 * @brief  Stampa la lista dei registri
 * @note   
 * @retval None
 */
void registers_list_print(){
    struct entries_register *cur;
    struct tm *date;
    
    cur = registers_list;
    printf("Registers list:\n");
    while(cur){
        date = malloc(sizeof(struct tm));
        memcpy(date,localtime(&cur->creation_date),sizeof(struct tm));
        if(!date)goto prosegui;
        printf("Registro : %d:%d:%d , ",date->tm_mday,date->tm_mon+1,date->tm_year+1900);
        printf(" %s, ",(cur->is_open)?"Aperto":"Chiuso");
        printf(" %s, ",(cur->is_completed)?"Completo":"Incompleto");
        printf(" Tot entries: %d",cur->count);
        printf(" Cod: %ld\n",cur->creation_date);
        
prosegui:
        free(date);
        cur = cur->next;
        
    }
}
/**
 * @brief  Restituisce le info di un registro come stringa
 * @note   
 * @param  *cur:entries_register puntatore al registro 
 * @retval char* stringa
 */
char* register_as_string(struct entries_register *cur){
    char *s;
    struct tm *date;
    s = malloc(255);
    date = malloc(sizeof(struct tm));
        memcpy(date,localtime(&cur->creation_date),sizeof(struct tm));
        if(!date)return "";
        sprintf(s,"Registro : %d:%d:%d , ",date->tm_mday,date->tm_mon+1,date->tm_year+1900);
        sprintf(s," %s, ",(cur->is_open)?"Aperto":"Chiuso");
        sprintf(s," %s, ",(cur->is_completed)?"Completo":"Incompleto");
        sprintf(s," Tot entries: %d",cur->count);
        sprintf(s," Cod: %ld\n",cur->creation_date);
        return s;
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
/**
 * @brief  Alloca e inizializza un nuovo vicino aprendo una connessione tcp con quest'ultimo
 * @note   
 * @param  id:int id del vicino 
 * @param  addr:in_addr indirizzo del vicino  
 * @param  port:int la porta della socket tcp del vicino 
 * @retval 
 */
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
        my_log_print(peer_log,"Errore nell'invio del messaggio di saluto! Ignoro socket\n");
        free(n);
        return NULL;
    }else{
        free(n);
        my_log_print(peer_log,"Errore nell'apertura della neighbour socket, DS non ancora aggiornato? Ignoro socket\n");
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
        n = neighbour_init_and_connect(id,addr,port);
    }else{//Ricevuto dal socket listener
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
    //printf("Tutto pronto!\n");
    pthread_mutex_unlock(&neighbors_list_mutex);
   // neighbors_list_print();
    return n;
}
/**
 * @brief  Dato un path come stringa, restituisce il nome del file
 * @note   vedi load_register
 * @param  path:char*  
 * @retval char* il nome del file come stringa
 */
char* get_file_name(char* path){
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
    timeinfo = localtime(&today);
    timeinfo->tm_sec = 0;
    timeinfo->tm_min = 0;
    timeinfo->tm_hour = 0;
    today = mktime(timeinfo);
    er->count=0;
    er->is_completed=0;
    er->entries = NULL;
    er->is_open = 1;
    er->next = NULL;
    do{
        er->creation_date = today;
        ret = registers_list_add(er);
        today+=86400; //passo al giorno successivo
    //La data di oggi potrebbe essere già stata chiusa
    }while(!ret);
    //Completiamo l'inizializzazione
    sprintf(filename,"%d_%d-%ld",er->is_completed,er->is_open,er->creation_date);
    er->path = malloc(sizeof(char)*(strlen(my_path)+strlen(filename)+7));//+7 perché considero anche '/' e "\0" e .txt
    strcpy(er->path,my_path);
    strcat(er->path,"/");
    strcat(er->path,filename);
    strcat(er->path,".txt");
    return er;
}
/**
 * @brief  Alloca e inizializza un registro chiuso in memoria e lo restituisce
 * * @note   vedi manage_register_backup
 * @param  cd:time_t 
 * @retval entries_register* puntatore al registro inizializzato
 */
struct entries_register* init_closed_register(time_t cd){
    char filename[20];
    struct entries_register  *er;
    er = malloc(sizeof(struct entries_register));
    er->creation_date = cd;
    er->count = 0;
    er->entries = NULL;
    er->f = 0;
    er->is_open = 0;
    er->is_completed=0;
    er->next = NULL;
    sprintf(filename,"%d_%d-%ld",er->is_completed,er->is_open,er->creation_date);
    er->path = malloc(sizeof(char)*(strlen(my_path)+strlen(filename)+7));
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
        my_log_print(ds_log,"Tutti i registri sono chiusi!\n");
        return; 
    }
    position = strlen(er->path);
    old_path = malloc(sizeof(char)*(strlen(er->path)+1));
    strcpy(old_path,er->path);
    er->is_open = 0;
    //Cambio nome al file per contrassegnarlo come chiuso
    while(er->path[position]!='-') position--;
    er->path[position-1] = '0';
    //printf("Rename:\nold: %s\nnew: %s\n",old_path,er->path);
    rename(old_path,er->path);
    free(old_path);
}

/**
 * @brief  Chiude il registro aperto e apre quello del giorno successivo.
 * @note   
 * @retval None
 */
void close_today_register(){
    struct entries_register *er;
    pthread_mutex_lock(&register_mutex);
close_loop://Recupero i giorni giorni persi aprendo e chiudendo i registri
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
/**
 * @brief  Restituisce la data del registro aperto
 * @note   
 * @retval timet_t copia della data
 */
time_t get_current_open_date(){
    time_t ret;
    pthread_mutex_lock(&register_mutex);
    ret =  current_open_date;
    pthread_mutex_unlock(&register_mutex);
    return ret;
}

/**
 * @brief  Carica il registro dal file current_open_date
 * @note   formato <secondi>.<nano>:<tipo>,<quantità>
 * @param  path:char* stringa del path 
 * @retval None
 */
struct entries_register* load_register(char* path){
    struct entries_register *er;
    struct entry *e;
    struct entry aux;
    char* s;
    memset(&aux,0,sizeof(struct entry));
    er = malloc(sizeof(struct entries_register));
    char* file_name = get_file_name(path);
    my_log_print(user_log,"Carico il registro con path %s\n ( il nome del file è %s )\n",path,file_name);
    if(sscanf(file_name,"%d_%d-%ld",&er->is_completed,&er->is_open,&er->creation_date)< 3){
        my_log_print(user_log,"Il File non ha il nome corretto, annullo il caricamento...\n");
        return NULL;
    }
    er->path = malloc(sizeof(char)*(strlen(path)+1));
    strcpy(er->path,path);
    er->entries = NULL;
    er->next = NULL;
    er->count = 0;
    er->f = fopen(path,"r");
    while(fscanf(
        er->f,
        "%ld.%ld:%c,%ld",
        &aux.timestamp.tv_sec,
        &aux.timestamp.tv_nsec,
        &aux.type,
        &aux.quantity)!=EOF){
        e = malloc(sizeof(struct entry));
        e->timestamp.tv_sec = aux.timestamp.tv_sec;
        e->timestamp.tv_nsec =aux.timestamp.tv_nsec;
        e->type = aux.type;
        e->quantity= aux.quantity;
        e->next = NULL;
        add_entry_in_register(er,e);
    }
    s = register_as_string(er);
    my_log_print(user_log,"Inserisco il registro nella lista:\n%s\n",s);
    free(s);
    registers_list_add(er);
    fclose(er->f);
    my_log_print(user_log,"Registri caricati\n");
    return er;
}

/**
 * @brief  
 * @note   
 * @param  buffer: 
 * @retval 
 */
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
        add_entry_in_register(er,e);
        index = size+1;
    }while(tot_size>index);
    pthread_mutex_unlock(&register_mutex); 
    return 1;
}

/**
 * @brief  Restituisce il vicino fornendo la socket
 * @note   
 * @param  socket:int la Socket del vicino richiesto
 * @retval neighbour* che punta il vicino, NULL se non esiste.
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
    close(n->socket);
    pthread_mutex_unlock(&fd_mutex);
    free(n);
    pthread_mutex_unlock(&neighbors_list_mutex);
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

struct sockaddr ds_addr;

/**
 * @brief  Crea le cartelle del peer 
 * @note   
 * @retval None
 */
void generate_work_folders(){
    char path[50];
    int result;
    sprintf(path,"./__%d",my_port);
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
/**
 * @brief  Inizializza la registers_list leggendo i file presenti nella cartella del peer
 * @note   
 * @retval None
 */
void registers_list_init(){
    DIR *d;
    struct dirent *dir;
    struct entries_register *er;
    char *path_n_file_name;
    d = opendir(my_path);
    if (d) {
        while ((dir = readdir(d)) != NULL) {//Leggo il nome del file
            if(strcmp((dir->d_name+strlen(dir->d_name)-3),"txt")==0){//Controllo che sia un txt
                path_n_file_name = malloc(sizeof(char)*(strlen(my_path)+strlen(dir->d_name)+2));//+2 perché considero anche '/' e "\n"
                strcpy(path_n_file_name,my_path);
                strcat(path_n_file_name,"/");
                strcat(path_n_file_name,dir->d_name);
                my_log_print(user_log,"Caricamento del register: %s",path_n_file_name);
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
/**
 * @brief  Alloca e inizializza le strutture dati per i log
 * @note   
 * @retval None
 */
void logs_init(){
    char* path;
    char* name;
    name = malloc(strlen(DEFAULT_DS_THREAD_NAME)+1);
    strcpy(name,DEFAULT_DS_THREAD_NAME);
    path = malloc(strlen(my_path)+strlen(DEFAULT_DS_COMUNITCATION_LOG_FILENAME)+1);
    strcpy(path,my_path);
    strcat(path,DEFAULT_DS_COMUNITCATION_LOG_FILENAME);
    ds_log = my_log_init(path,name);
    free(path);
    free(name);

    name = malloc(strlen(DEFAULT_PEER_THREAD_NAME)+1);
    strcpy(name,DEFAULT_PEER_THREAD_NAME);
    path = malloc(strlen(my_path)+strlen(DEFAULT_PEER_COMUNICATION_LOG_FILENAME)+1);
    strcpy(path,my_path);
    strcat(path,DEFAULT_PEER_COMUNICATION_LOG_FILENAME);
    peer_log = my_log_init(path,name);
    free(path);
    free(name);

    name = malloc(strlen(DEFAULT_USER_THREAD_NAME)+1);
    strcpy(name,DEFAULT_USER_THREAD_NAME);
    path = malloc(strlen(my_path)+strlen(DEFAULT_USER_LOG_FILENAME)+1);
    strcpy(path,my_path);
    strcat(path,DEFAULT_USER_LOG_FILENAME);
    user_log = my_log_init(path,name);
    free(path);
    free(name);

}
/**
 * @brief  Dealloca le strutture dati dei log
 * @note   
 * @retval None
 */
void logs_free(){
    my_log_free(ds_log);
    my_log_free(peer_log);
    my_log_free(user_log);
}

int get_loop_flag(){
    int ret;
    pthread_mutex_lock(&set_get_mutex);
    ret = loop_flag;
    pthread_mutex_unlock(&set_get_mutex);
    return ret;
}
void set_loop_flag(int flag){
    pthread_mutex_lock(&set_get_mutex);
    loop_flag = flag;
    pthread_mutex_unlock(&set_get_mutex);
    
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
    //Flag per indicare se è in corso una operazione Request ( Blocca l'input utente )
    req_busy_flag = 0;
    ds_comunication_thread = 0;
    tcp_comunication_thread = 0;
    FD_ZERO(&master);
    FD_ZERO(&to_read);
    neighbors_list = NULL;
    generate_work_folders();
    logs_init();
    registers_list_init();
    pthread_cond_init(&request_busy,NULL);
    pthread_mutex_init(&request_mutex,NULL);
    pthread_mutex_init(&neighbors_list_mutex,NULL);
    pthread_mutex_init(&register_mutex,NULL);
    pthread_mutex_init(&fd_mutex,NULL);
    pthread_mutex_init(&set_get_mutex,NULL);
}

/**
 * @brief  Richiama tutte le funzioni per deallocare le strutture dati globali
 * @note   
 * @retval None
 */
void globals_free(){
    close(s_socket);
    requestes_list_free();
    registers_list_free();
    neighbors_list_free();
    logs_free();
}

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

/**
 * @brief  Controlla il formato della data assumendone la sua correttezza
 * @note   vedi add_new_request
 * @param  *dates: 
 * @retval int -1 nessuna data, 0 start e end presenti , 1 solo start presente , 2 solo end presente
 */
int check_date_format(char *dates){
    char *aux;
    char* cpy;
    int ret = 0; //Sia start che end
    if(dates==NULL)return -1;//nessuna data
    cpy = malloc(strlen(dates)+1);
    strcpy(cpy,dates);
    if(strcmp(cpy,"")==0){
        free(cpy);
        return -1;
    }
    aux = strtok(cpy,",");
    //my_log_print(user_log,"Controllo prima data: %s\n",aux);
    if(strlen(aux)==1) ret=2;//Solo end
    else {
        aux = strtok(NULL,",");
        if(strlen(aux)==1) ret=1;//Solo start ( dates+strlen(aux)+1 punto alla parte di stringa che mi interessa )
    }
  
    free(cpy);
    return ret;
}

/**
 * @brief  Genera una nuova richiesta e la inserisce nella requestes_list
 * @note   vedi get_last_closed_register, check_date_format, init_request, requestes_list_add
 * @param  rt:char tipo di richiesta ( t:totale v:variazione ) 
 * @param  t:char tipo di entry ( t:tampone n:nuovo caso ) 
 * @param  dates:char* stringa contenente la data dd:mm:yyyy-dd:mm:yyyy, * per indicare limite superiore o inferiore (non entrambi), per comprendere tutti i registri chiusi lasciare NULL
 * @retval puntatore request* alla richiesta appena inizializzata, NULL in caso di errore
 */
struct request* add_new_request(char rt, char t, char* dates){
    struct request *r;
    time_t *start,*end;
    struct entries_register *last_closed_reg;
    struct tm s,e;
    start =NULL;
    end = NULL;
    my_log_print(user_log,"r_type = %c e_type = %c\n",rt,t);
    if(rt!='t' && rt!='v'){
        printf("Tipo di richiesta errata \n");
        return NULL;
    }
    if(t!='t' && t!='n'){
        printf("Tipo entry errata\n");
        return NULL;
    }
    last_closed_reg = get_last_closed_register();
    my_log_print(user_log,"last_closed_reg = %s\n",ctime(&last_closed_reg->creation_date));
    if(last_closed_reg==NULL){
        printf("Non esiste ancora un registro chiuso!\n");
        return NULL;
    }
    end = malloc(sizeof(time_t));
    start = malloc(sizeof(time_t));
    *end = -1;
    *start = -1;
    memset(&s,0,sizeof(struct tm));
    memset(&e,0,sizeof(struct tm));
    switch(check_date_format(dates)){
        case 0://start ed end presenti
            printf("start e end presenti: %s\n",dates);
            
            if(sscanf(dates,"%d:%d:%d,%d:%d:%d",
            &s.tm_mday,
            &s.tm_mon,
            &s.tm_year,
            &e.tm_mday,
            &e.tm_mon,
            &e.tm_year)<6){
                printf("Date non valide\nFormato corretto: dd1:mm1:yyyy1,dd2:mm2:yyyy2\n");
                return NULL;
            }
            if(s.tm_mday<1 || s.tm_mday>31 || s.tm_mon<1 || s.tm_mon>12 || s.tm_year<1900){
                printf("Data Inizio non valida\nFormato corretto: dd1:mm1:yyyy1,dd2:mm2:yyyy2\n");
                return NULL;
            }
            if(e.tm_mday<1 || e.tm_mday>31 || e.tm_mon<1 || e.tm_mon>12 || e.tm_year<1900){
                printf("Data Fine non valida\nFormato corretto: dd1:mm1:yyyy1,dd2:mm2:yyyy2\n");
                return NULL;
            }
            s.tm_mon -=1;
            e.tm_mon -=1;
            s.tm_year -=1900;
            e.tm_year -=1900;
            s.tm_hour -= 1;
            s.tm_min = 0;
            s.tm_sec = 0;
            e.tm_hour -= 1;
            e.tm_min = 0;
            e.tm_sec = 0;
            *start = mktime(&s);
            *end = mktime(&e);
            if(*start>*end){
                printf("Data inizio è più grande di data fine! Intendevi il contrario?\n");
                return NULL;
            }
        break;
        case 1://solo start presente
        printf("start presente: %s\n",dates);
        
        if(sscanf(dates,"%d:%d:%d,*",
            &s.tm_mday,
            &s.tm_mon,
            &s.tm_year)<3){
                printf("Date non valide\nFormato corretto: dd1:mm1:yyyy1,dd2:mm2:yyyy2\n");
                return NULL;
            }
            s.tm_mon -=1;
            s.tm_year -=1900;
            s.tm_hour -= 1;
            s.tm_min = 0;
            s.tm_sec = 0;
            *start = mktime(&s);
            
            
        break;
        case 2://solo end presente
            printf("end presente: %s\n",dates);
            if(sscanf(dates,"*,%d:%d:%d",
                &e.tm_mday,
                &e.tm_mon,
                &e.tm_year)<3){
                    printf("Date non valide\nFormato corretto: dd1:mm1:yyyy1,dd2:mm2:yyyy2\n");
                    return NULL;
                }
                e.tm_mon -=1;
                e.tm_year -=1900;
                e.tm_hour -= 1;
                e.tm_min = 0;
                e.tm_sec = 0;
                *end = mktime(&e);
        break;
    }
    if(*end==-1)*end = last_closed_reg->creation_date;
    if(*start==-1)*start = get_first_closed_register()->creation_date;
    my_log_print(user_log,"Dopo lo switch\n");
    if(last_closed_reg->creation_date<*end){
        printf("Data fine troppo grande, aggiornata con l'ultimo registro aperto\n");
        *end = last_closed_reg->creation_date;
    }
    my_log_print(user_log,"fcr: %s, lcr:%s\n",(start!=NULL)?ctime(&get_first_closed_register()->creation_date):"Nessuna",ctime(&last_closed_reg->creation_date));
    my_log_print(user_log,"start: %s, end:%s\n",(start!=NULL)?ctime(start):"Nessuna",ctime(end));
    my_log_print(user_log,"Prima di init quest\n");
    r = init_request(t,rt,start,end,NULL,NULL);
    if(r)requestes_list_add(r);
    free(start);
    free(end);
    return r;
}

/**
 * @brief  Inserisce 10 entry di prova nel registro aperto
 * @note   vedi user_loop e ds_comunicatio    n_loop
 * @retval None
 */
void test_add_entry(){
    int rand_quantity;
    srand(time(NULL));
    for(int i =0 ; i<10 ;i++){
        rand_quantity = rand() % 100 + 1;
        create_entry((rand_quantity%2==0)?'n':'t',rand_quantity);
    }
}

void send_backups_to_all();
void send_request_to_all(struct request *r,int *avoid_socket);
void* ds_comunication_loop(void*arg);
void send_byebye_to_all();
char* get_result_if_exist(struct request*r);
/**
 * @brief  Loop con il quale l'utente interagisce con il programma
 * @note   
 * @retval None
 */
void user_loop(){
    void* ret;
    char*result;
    char confirm;
    char msg[40];
    int port;
    int args_number;
    int command_index;
    char args[4][PEER_MAX_COMMAND_SIZE];
    char type;
    long quantity;
    struct request* r;
    printf(PEER_WELCOME_MSG);
    while(get_loop_flag()){
        wait_request_elaboration();
        printf(">> ");
        fgets(msg, 40, stdin);
        my_log_print(user_log,"User ha scritto: %s",msg);
        args_number = sscanf(msg,"%s %s %s %s",args[0],args[1],args[2],args[3]);
        my_log_print(user_log,"Riconosciuti %d args\n",args_number);
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
                my_log_print(user_log,"Riconosciuto comando Start\n");
                if(my_id!=-1){
                    printf("Sei già connesso alla rete!\nI tuoi vicini:\n");
                    my_log_print(user_log,"User già collegato alla rete, annullo.\n");
                    neighbors_list_print();
                    break;
                }
                if(args_number < 2){
                    printf("Manca la porta del DS\n");
                    my_log_print(user_log,"Errore, troppi pochi argomenti, annullo.\n");
                    break;
                }
                port = atoi(args[1]);
                lock_user_input();
                if(pthread_create(&ds_comunication_thread,NULL,ds_comunication_loop,(void*)&port)){
                    perror("Errore nella creazione del thread\n");
                    my_log_print(user_log,"Errore fatale, impossibile creare il thread per comunicare con il DS\n");
                    exit(EXIT_FAILURE);
                }
                test_add_entry();
                break;
            //__add__
            case 2:
                my_log_print(user_log,"Riconosciuto comando Add\n");
                if(my_id==-1){
                    printf("Non sei connesso alla rete!\n");
                    my_log_print(user_log,"Utente non connesso alla rete, operazione ignorata\n");
                    break;
                }
                type = *args[1];
                if( type!='n' && type!='t'){
                    printf("Tipo non valido, accettato solo n e t\n");
                    my_log_print(user_log,"Argomento 'type' pasato sbagliato: %c\n",type);
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
                    my_log_print(user_log,"Entry annulata dall'utente\n");
                    break;
                }
                if(create_entry(type,quantity)==0){
                    printf("Impossibile inserire una nuova entry!\n");
                    my_log_print(user_log,"Errore durante la creazione della entry, annulato\n");
                    break;
                }
                printf("Inserimento avvenuto con successo!\n");
            break;
            //__get__
            case 3:
                my_log_print(user_log,"Riconosciuto comando Get\n");
                if(my_id==-1){
                    printf("Non sei connesso alla rete!\n");
                    my_log_print(user_log,"Utente non connesso alla rete, operazione ignorata\n");
                    break;
                }
                if(args_number<3){
                    printf("Inserire aggr type period\n");
                    my_log_print(user_log,"Args passati < 3 , 'get' annulato\n");
                    break;
                }
                if(args_number<4){
                    my_log_print(user_log,"Quarto argomento non trovato, sostituito con stringa vuota\n");
                    strcpy(args[3],"");
                }
                r = add_new_request(args[1][0],args[2][0],args[3]);
                if(!r){
                    printf("Richiesta scartata\n");
                    my_log_print(user_log,"Tipo di richiesta errata\n");
                    break;
                }
                my_log_print(user_log,"Nuova Request inserita con successo: %ld \n",r->timestamp);
                result = get_result_if_exist(r);
                if(result!=NULL){
                    printf("Calcolo già effettuato in precedenza:\n%s",result);
                    my_log_print(user_log,"Calcolo già effettuato in precedenza:\n%s",result);
                    free(result);
                    break;
                }
                printf("File non esistente, controllo i registri per poterlo calcolare\n");
                my_log_print(user_log,"File non esistente, avvio controllo dei registri per calcolo\n");
                lock_user_input();
                result = elab_request(r);
                if(result!=NULL){
                    printf("Calcolo effettuato localmente.\nRisultato salvato in:\n%s\n",result);
                    my_log_print(user_log,"Elaborato salvato in: %s\n",result);
                    free(result);
                    unlock_user_input();
                    break;
                }
                printf("Non abbiamo tutti i dati, chiedo ai vicini il FILE\n");
                my_log_print(user_log,"Registri non completi, richiesta ai vicini del FILE richiesto\n");
                if(my_id==-1){
                    printf("Non sei connesso alla rete!\n");
                    break;
                }
                cur_FILE_request_set(r);
                send_FILE_request();
                break;
            //__esc__
            case 4:
                
                my_log_print(user_log,"Riconosciuto comando Esc\n");
                printf("Segnale l'uscita al DS...\n");
                ds_option_set('b');
                printf("Chiusura in corso...\n");
                printf("Invio il backup a tutti i vicini...\n");
                send_backups_to_all();
                printf("Invio byebye a tutti...\n");
                send_byebye_to_all();
                set_loop_flag(0);
                printf("Attendo il thread UDP\n");
                pthread_join(ds_comunication_thread,&ret);
                printf("Attendo il thread TCP\n");
                pthread_join(tcp_comunication_thread,&ret);
                printf("Socket chiusa\n");
                
            break;
            case 5://showpeers
                my_log_print(user_log,"Riconosciuto comando Showpeers\n");
                neighbors_list_print();

            break;
            case 6://showregs
                my_log_print(user_log,"Riconosciuto comando Showregs\n");
                registers_list_print();

            break;
            //__comando non riconosciuto__
            default:
                my_log_print(user_log,"Comando non valido\n");
                printf("Comando non riconosciuto!\n");
            break;
        }

    }


}



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
    my_log_print(ds_log,"Nuovo vicino id %d e socket %d\n",n->id,n->socket);
    /*GENERO IL MESSAGGIO E LO MANDO DOPO AVER MANDATO IL PRIMO PACCHETTO*/
    len = generate_greeting_message(&buffer);
    my_log_print(ds_log,"Il messaggio è lungo %u ed è %s\n",len,buffer);
    len = htonl(len);
    operations = (unsigned int)operation;
    operations = htonl(operations);
    first_packet = operations;
    first_packet = first_packet<<32 | len;
    len = ntohl(len);
    my_log_print(ds_log,"First packet %lu, ed è lungo %lu\n",first_packet,sizeof(first_packet));
    //Mando il primo pacchetto con l'operazione richiesta
    //e la lunghezza del secondo pacchetto
    
    if(send(n->socket,(void*)&first_packet,sizeof(first_packet),0)<0){
        free(buffer);
        perror("Errore in fase di invio\n");
        return 0;
    }
    if(send(n->socket,(void*)buffer,(size_t)len,0)<0){//size_t è un unsigned long
        free(buffer);
        perror("Errore in fase di invio\n");
        return 0;
    }
    free(buffer);
    my_log_print(ds_log,"Saluto inviato!\n");
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
    
    
    strcpy(*msg,name);
    if(size == 0){
        free(*msg);
        msg = NULL;
        return size;
    }
    fread((void*)(*msg+strlen(name)),sizeof(char),size,er->f);
    (*msg)[size+strlen(name)]='\0';
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
    my_log_print(user_log,"Addio inviato a %d\n",n->id);
    return 1;
}



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
      //La data passata ( e ) altrimenti vogliamo spedire tutto e consideriamo anche il registro aperto ( caso esc )  
      end = (e==NULL)?get_open_register()->creation_date:*e;
      //La data passata ( s ) altrimenti il primo registro chiuso se esiste, altrimenti l'unico registro aperto.
      start = (s==NULL)?er->creation_date:*s;
    //Per ogni registro nel range leggo le entry in un msg e via via concateno tutto in tot_msg
    //Generando un unico messaggio con i registri e le relative entry interessate
    while(er){
        if(er->creation_date<start){//Data non compresa
            my_log_print(peer_log,"creation_date: %ld <  start=%ld  ignoro per backup\n",er->creation_date,start);
            er = er->next;
            continue;
        }
        if(er->creation_date>end)break;//Abbiamo superato la data di fine

        sprintf(name,"%ld,%u\n",er->creation_date,er->count);
        //my_log_print(peer_log,"Prima della fopen\n");
        er->f=fopen(er->path,"r");
        if(er->f==NULL){
            er = er->next;
            continue;
        }
        //Controllo la dimensione del file
        //my_log_print(peer_log,"Prima dell fseek\n");
        fseek(er->f, 0L, SEEK_SET);
        fseek(er->f, 0L, SEEK_END);
        size = ftell(er->f);
        fseek(er->f, 0L, SEEK_SET);
        if(msg) free(msg);
        msg = malloc(size+strlen(name)+1);
        //my_log_print(peer_log,"Prima di strcpy\n");
        strcpy(msg,name);
        if(size == 0){//File vuoto
            free(msg);
            msg = NULL;
            er = er->next;
            continue;
        }
        fread((void*)(msg+strlen(name)),sizeof(char),size,er->f);
        msg[size+strlen(name)]='\0';
        //my_log_print(peer_log,"Parte del msg\n %s\n",msg);
        if(tot_msg == NULL){//Prima volta che alloco tot_msg
        //my_log_print(peer_log,"Prima allocazione\n");
            tot_msg = malloc(strlen(msg)+1);
            //my_log_print(peer_log,"Prima della cpy\n");
            strcpy(tot_msg,msg);      
        }else{//Realloco tot_msg aggiungendoci le nuove entry del registro letto
        //my_log_print(peer_log,"Prima di realloc\n");
            tot_msg = realloc(tot_msg,(strlen(tot_msg)+strlen(msg))+1);
            //tot_msg[strlen(tot_msg)+strlen(msg)] = '\0';
        //my_log_print(peer_log,"Prima di strcat con tot_msg = %s\n",tot_msg);
            strncat(tot_msg,msg,strlen(msg));
        }
        fclose(er->f);
        er = er->next;
        
    }
    tot_msg[strlen(tot_msg)]= '\0';
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
    n = neighbors_list;
    if(n==NULL){
        my_log_print(user_log,"Nessun vicino!\n");
        goto end_send_to_all;
        return;
    }
    char operation = 'U';//Update!
    my_log_print(user_log,"Genero il messaggio di backup\n");
    char *msg = generate_backup_message(NULL,NULL);
    my_log_print(peer_log,"Il messaggio generato da inviare: %s\n",msg);
    if(msg==NULL){
        my_log_print(user_log,"Niente da inviare\n");
        goto end_send_to_all;
        return;
    }
    len = strlen(msg)+1;//Per il carattere terminatore
    len = htonl(len);
    operations = (unsigned int)operation;
    operations = htonl(operations);
    first_packet = operations;
    first_packet = first_packet<<32 | len;
    len = ntohl(len);
    while(n){
    //Mando il primo pacchetto con l'operazione richiesta
    //e la lunghezza del secondo pacchetto  
        if(send(n->socket,(void*)&first_packet,sizeof(first_packet),0)<0){
            perror("Errore in fase di invio");
            n=n->next;
            continue;
        }
        if(send(n->socket,(void*)msg,len,0)<0){//+1 per carattere terminatore
            perror("Errore in fase di invio");
            n=n->next;
            continue;
        }
        my_log_print(user_log,"Backup inviato a %d porta=%u\n",n->id,n->addr.sin_port);
        n=n->next;
    }
free(msg);
end_send_to_all:
    pthread_mutex_unlock(&neighbors_list_mutex);
    my_log_print(user_log,"Completato\n");
}

/**
 * @brief  Genera il messaggio di richiesta da inviare
 * @note   
 * @param  *r:request la request dalla quale generare il messaggio 
 * @retval char* puntatore alla stringa
 */
char* generate_request_message(struct request *r){
    char* msg;
    char aux[512];
    long len;
    if(r==NULL)return NULL;
    if(sprintf(aux,"%d,%ld,%ld,%ld,%c,%c\n",r->id,r->timestamp,r->date_start,r->date_end,r->entry_type,r->req_type)<6){
        my_log_print(peer_log,"Generate_Header_Rrquest: Errore nella lettura dei parametri della request\n");
        return NULL;
    }
    len = strlen(aux);
    if(len==0)return NULL;
    msg = malloc(len+1);
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
    uint64_t first_packet=0; // contiene la lunghezza del prossimo messaggio e del codice
    uint32_t len; // lunghezza messaggio
    uint32_t operations=0;
    struct neighbour *n;
    my_log_print(peer_log,"Invio una richiesta dati ( flooding )\n");
    pthread_mutex_lock(&neighbors_list_mutex);
    char operation = 'R';//Request!
    char *msg = generate_request_message(r);
    my_log_print(peer_log,"Il messaggio: %s",msg);
    n = neighbors_list;
    if(n==NULL){
        my_log_print(peer_log,"Nessun vicino!\n");
        goto end_req_to_all;
        return;
    }
    if(msg==NULL){
        my_log_print(peer_log,"Niente da inviare\n");
        goto end_req_to_all;
        return;
    }
    len = strlen(msg)+1;
    len = htonl(len);
    operations = (unsigned int)operation;
    operations = htonl(operations);
    first_packet = operations;
    first_packet = first_packet<<32 | len;
    len = ntohl(len);
    my_log_print(peer_log,"Generato il messaggio di lunghezza %dl\n messaggio:\n%s\n",len,msg);
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
        if(send(n->socket,(void*)msg,len,0)<0){//+1 per carattere terminatore
            perror("Errore in fase di invio");
            n=n->next;
            continue;
        }
        my_log_print(peer_log,"Request inviata a %d socket=%d porta=%u\n",n->id,n->socket,n->addr.sin_port);
        n=n->next;
    }
    
end_req_to_all:
    pthread_mutex_unlock(&neighbors_list_mutex);
    if(msg) free(msg);
    my_log_print(peer_log,"\n");
}


/**
 * @brief  Controlla se il risultato della richiesta è già presente come file
 * @note   
 * @param  request*r: la richiesta della quale cerchiamo la richiesta
 * @retval char* stringa contenente il risultato, oppure NULL se non presente
 */
char* get_result_if_exist(struct request*r){
    char file_name[128];
    char* msg;
    char* path;
    int size;
    msg = NULL;
    FILE *f;
    if(r==NULL)return NULL;
    sprintf(file_name,"/%c%c-%ld_%ld.res",r->req_type,r->entry_type,r->date_start,r->date_end);
    path = malloc(strlen(my_path)+strlen(file_name)+1);
    strcpy(path,my_path);
    strcat(path,file_name);
    //printf("Path: %s\n",path);
    f = fopen(path,"r");
    if(!f)return NULL; //File inesistente
    fseek(f, 0L, SEEK_SET);
    fseek(f, 0L, SEEK_END);
    size = ftell(f);
    if(size==0)return NULL; //File vuoto!
    fseek(f, 0L, SEEK_SET);
    msg = malloc(size);
    fread((void*)(msg),sizeof(char),size,f);
    fclose(f);
    return msg;
    //Leggo il file come stringa
}


/**
 * @brief  Invia i dati richiesti da una socket alla ret_socket
 * @note   
 * @param  *r:request la richiesta 
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
        my_log_print(peer_log,"Non posso inviare i dati alla request: La request è NULL!\n");
        return;
    }
    my_log_print(peer_log,"Invio i dati richiesti per la request:\n");
    backup = generate_backup_message(&r->date_start,&r->date_end);
    my_log_print(peer_log,"Il backup generato da inviare: %s\n",backup);
    req_header = generate_request_message(r);
    if(!backup || !req_header){
        my_log_print(peer_log,"Niente da inviare\n");
        return;
    }
    my_log_print(peer_log,"La dimensione da allocare è %dB",strlen(backup)+strlen(req_header)+1);
    msg =malloc(strlen(backup)+strlen(req_header)+1);
    if(!msg){
        my_log_print(peer_log,"Errore durante l'allocazione del messaggio\n");
        return;
    }
    strcpy(msg,req_header);
    strcat(msg,backup);
    msg[strlen(backup)+strlen(req_header)]='\0';
    len = strlen(msg)+1;
    len = htonl(len);
    operations = (unsigned int)operation;
    operations = htonl(operations);
    first_packet = operations;
    first_packet = first_packet<<32 | len;
    len = ntohl(len);
    //Mando il primo pacchetto con l'operazione richiesta
    //e la lunghezza del secondo pacchetto  
        if(send(r->ret_socket,(void*)&first_packet,sizeof(first_packet),0)<0){
            perror("Errore in fase di invio");
            goto req_end;
        }
        if(send(r->ret_socket,(void*)msg,len,0)<0){//+1 per carattere terminatore
            perror("Errore in fase di invio");
            goto req_end;
        }
req_end:
        free(backup);
        free(req_header);
        free(msg);
}


/**
 * @brief  Setta tutti i registri della lista come completi
 * @note   
 * @param  *r:request la lista dei registri da flaggare come completi 
 * @retval None
 */
void check_all_registers_as_completed(struct request *r){
    struct entries_register *er;
    if(registers_list == NULL) return;
    er = registers_list;
    char *old_path;
    int position;
    while(er->creation_date<r->date_start) er = er->next;
    while(er->creation_date<=r->date_end){
        if(er->is_completed)goto carac_loop_end;
        er->is_completed = 1;
        position = strlen(er->path);
        old_path = malloc(sizeof(char)*(strlen(er->path)+1));
        strcpy(old_path,er->path);
        while(er->path[position]!='-') position--;
        er->path[position-2] = '1';
        rename(old_path,er->path);
        free(old_path);
    carac_loop_end:
        er = er->next;
    }
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
    char t,rt;//type, req_type
    time_t s,e,tp;//start,end,timestamp
    char aux[128];
    sscanf(buffer,"%s",aux);
    index+=strlen(aux)+1;
    sscanf(aux,"%d,%ld,%ld,%ld,%c,%c",&id,&tp,&s,&e,&t,&rt);
    r = requestes_list_get(tp,id);
    if(r==NULL){//Non dovrebbe accadere, ma la gestiamo
        my_log_print(peer_log,"Risposta di una richiesta inesistente\n");
        return 0;
    }
    n = get_neighbour_by_socket(socket);
    if(n==NULL){
        my_log_print(peer_log,"Nessun vicino corrisponde alla socket passata!\n");
        return 0;
    }
    if(!flooding_list_check_flooding_mate(r->flooding_list,n->id,socket)){
        my_log_print(peer_log,"Il vicino %d non è in lista!\n",n->id);
        /*Qualcosa è andato storto, abbiamo ricevuto un pacchetto
        * non aspettato. Lo ignoriamo.
        */
        return 0;
    }
    manage_register_backup(buffer+index);
    if(flooding_list_all_checked(r->flooding_list)){
        if(r->ret_socket==-1){//Sono io che ho iniziato la richiesta
            my_log_print(peer_log,"Sono io che ho fatto la richiesta, procedo ad elaborarla\n");
            check_all_registers_as_completed(r);
            printf("Risultato salvato in : %s\n",elab_request(r));
            unlock_user_input();
        }else{//Invio il risultato alla ret_socket di r
            my_log_print(peer_log,"Inoltro i dati\n");
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
    len = strlen(msg)+1;
    len = htonl(len);
    operations = (unsigned int)operation;
    operations = htonl(operations);
    first_packet = operations;
    first_packet = first_packet<<32 | len;
    len = ntohl(len);
    my_log_print(peer_log,"Request IGNORE:\n -lunghezza %u\n -messaggio:\n%s\n",len,msg);
    if(send(socket,(void*)&first_packet,sizeof(first_packet),0)<0){
            perror("Errore in fase di invio");
            return 0;
        }
    if(send(socket,(void*)msg,len,0)<0){//+1 per carattere terminatore
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
 * @retval int 0 già presente, 1 nuova richiesta inserita con successo, -1 in caso di errore
 */
int manage_new_request(char *buffer,int socket){
    struct request *r;
    int id;
    char t,rt;//type,req_type
    time_t s,e,tp;//start,end,timestamp  
    if(sscanf(buffer,"%d,%ld,%ld,%ld,%c,%c",&id,&tp,&s,&e,&t,&rt)<6){
        my_log_print(peer_log,"Errore nella lettura dei dati della request, richiesta scartata");
        return -1;
    }
    r = requestes_list_get(tp,id);
    if(r){
        my_log_print(peer_log,"Richiesta già presente, gestisco il loop\n");
        /* Invio una risposta una risposta speciale
        *  per togliermi dai flooding mates.
        */
       my_log_print(peer_log,"Invio un messaggio 'Ignora' alla socket %d\n",socket);
        if(!send_ignore_my_answer(buffer,socket)){
            my_log_print(peer_log,"Impossibile inviare la richiesta 'Ignora' alla socket=%d\n",socket);
        }
        return 0;
    }
    //Una nuova richiesta!
    r = init_request(t,rt,&s,&e,&socket,&id);
    requestes_list_add(r);
    if(r->flooding_list==NULL){
    //Non devo inoltrare la richiesta a nessuno
    //Quindi ripondo subito
        send_request_data(r);
    }else{
        send_request_to_all(r,&socket);
    }
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
    char t,rt;//type req_type
    struct neighbour *n;
    time_t s,e,tp;//start,end,timestamp  
    sscanf(buffer,"%d,%ld,%ld,%ld,%c,%c",&id,&tp,&s,&e,&t,&rt);
    r = requestes_list_get(tp,id);
    if(r == NULL){
        /*Qualcosa è andato storto ho ricevuto il messaggio
        * di ignorare una risposta a una richiesta che non possiedo*/
       return 0;
    }
    n = get_neighbour_by_socket(socket);
    if(n==NULL){
        my_log_print(peer_log,"Nessun vicino corrisponde alla socket passata!\n");
        return 0;
    }
    //Lo flaggo e ignoro
    my_log_print(peer_log,"Spunto %d dai flooding mate\n",socket);
    flooding_list_check_flooding_mate(r->flooding_list,n->id,socket);
    my_log_print(peer_log,"Controllo se tutti i flooding mate hanno risposto\n");
    if(flooding_list_all_checked(r->flooding_list)){
        my_log_print(peer_log,"Tutto pronto! Procedo ad elaborare oppure inoltrare i dati\n");
        if(r->ret_socket==-1){//Sono io che ho iniziato la richiesta
            my_log_print(peer_log,"Sono io che ho fatto la richiesta, procedo ad elaborarla\n");
            check_all_registers_as_completed(r);
            printf("Risultato salvato in : %s\n",elab_request(r));
            unlock_user_input();
        }else{//Invio il risultato alla ret_socket di r
            my_log_print(peer_log,"Inoltro i dati\n");
            send_request_data(r);
        }
    }
    return 1;
}
/**
 * @brief  Cerca un registro nella registers_list e la restituisce
 * @note   
 * @param  cd:time_t data del registro che cerchiamo 
 * @retval entries_register* puntatore al registro trovato, NULL se non esiste
 */
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
 * @retval int 1 almeno un inserimento, 0 nessun inserimento
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
    //printf("Il messaggio parziale di A:\n%s\n",msg);
    my_log_print(peer_log,"Gestione del messaggio di backup\n");
    pthread_mutex_lock(&register_mutex);
    while(sscanf(msg+len,"%s",aux)==1){
        len += strlen(aux)+1;
        sscanf(aux,"%ld,%u",&cd,&loop_size);
        er = get_register_by_creation_date(cd);
        my_log_print(peer_log,"Lavoro con il registro %d\n",er->creation_date);
        for(int i=0;i<loop_size;i++){
            sscanf(msg+len,"%s",aux);
            len+=strlen(aux)+1;
            e = malloc(sizeof(struct entry));
            my_log_print(peer_log,"Leggo riga %s\n",aux);
            if(sscanf(aux,"%ld.%ld:%c,%ld",
            &e->timestamp.tv_sec,
            &e->timestamp.tv_nsec,
            &e->type,
            &e->quantity)==4){
                if(er==NULL){
                    my_log_print(peer_log,"Il registro corrente è NULL, lo inizializzo\n");
                    //Costruisco un registro di cui non conoscevo l'esistenza e continuo
                    er = init_closed_register(cd);
                    registers_list_add(er);
                }
                ret = add_entry_in_register(er,e);
                if(ret){//È stato effettivamento inserito ( non è un duplicato )
                    //Controlliamo che il registro non sia chiuso
                    my_log_print(peer_log,"Entry inserita con successo!\n");
                    final_ret = final_ret || ret;
                    er->f=fopen(er->path,"a");
                    fprintf(er->f,"%ld.%ld:%c,%ld\n",e->timestamp.tv_sec,
                    e->timestamp.tv_nsec,
                    e->type,
                    e->quantity);
                    //Infine richiudiamo il registro
                    fclose(er->f);
                }else{
                    my_log_print(peer_log,"Entry già presente, continuo...\n");
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
 * @brief  Aggiorna la lista dei vicini
 * @note   vedi ds_comunication_loop
 * @param  msg:char* stringa contenente la lista dei vicini
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
    //printf("Il mio id = %d\n",my_id);
    index = strlen(aux)+1;
    for(int i = 0; i<entries_number;i++){
        sscanf(msg+index,"%s\n",aux);
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
    pthread_mutex_lock(&set_get_mutex);
    c = ds_option;
    pthread_mutex_unlock(&set_get_mutex);
    return c;
}
/**
 * @brief  Setta ds_option che specifica il servizio da richiedere al DS alla prossima iterazione del ds_comunication_loop
 * @note   vedi ds_comunicazion_llop
 * @param  c:char tipo di opzione 
 * @retval None
 */
void ds_option_set(char c){
    pthread_mutex_lock(&set_get_mutex);
    ds_option = c;
    pthread_mutex_unlock(&set_get_mutex);
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
    int cur_bytes;
    unsigned int total_bytes=0;
    unsigned int packets_recived=0;
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
        unlock_user_input();
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
    
    while(get_loop_flag()){
        option = ds_option_get();
        //Richiesta vicini
        sprintf(buffer,"%u,%d,%ld,%c",my_addr.s_addr,my_port,current_open_date,option);
        my_log_print(ds_log,"Invio %c al DS\n",option);
        sendto(socket,&buffer,strlen(buffer)+1,0,(struct sockaddr*)&ds_addr,ds_addrlen);
        ds_to_read = ds_master;
        timeout.tv_sec = DS_COMUNICATION_LOOP_SLEEP_TIME;
        ret = select(socket+1,&ds_to_read,NULL,NULL,&timeout); //Abbiamo solo una socket nella lista
        if(ret==1){
            cur_bytes = recvfrom(socket,buffer,DS_BUFFER,0,(struct sockaddr*)&ds_addr,&ds_addrlen);
            if(cur_bytes<0){
                printf("DS_Thread: Errore nella ricezione");
                continue;
            }
            total_bytes+=cur_bytes;
            packets_recived++;
            if(option=='x'){//Richiesta della lista dei vicini
                my_log_print(ds_log,"Ricevuta lista dei peer:\n %s",buffer);
                update_neighbors(buffer);
                my_log_print(ds_log,"Imposto il messaggio da inviare al DS: 'Refresh'-> 'r'\n");
                ds_option_set('r');//Il loop passa in modalità sincronizzazione ( refresh )
            }else if(option == 'r'){
                my_log_print(ds_log,"Ho ricevuto il comando di chiudere il registro, apro il successivo\n");
                sscanf(buffer,"%ld\n",&time_recived);
                if(time_recived > get_current_open_date()){//dobbiamo sincronizzarci
                    set_current_open_date(time_recived);
                    close_today_register();
                    my_log_print(ds_log,"Aperto nuovo registro : %ld\n",time_recived);
                    my_log_print(ds_log,"Simulo 10 nuove entrate casuali\n");
                    test_add_entry();   

                }
            }
        }
        if(option == 'b'){//byebye
            break;
        }
    }
    my_log_print(ds_log,"Chiusura in corso...\n");
    my_log_print(ds_log,"Info traffico:\nBytes totali ricevuti %uB \nPacchetti ricevuti: %u\nMedia per pacchetto: %uB\n",total_bytes,packets_recived,(total_bytes/packets_recived));   
    close(socket);
    my_log_print(ds_log,"FINE\n");;
    pthread_exit(NULL);
}

/**
 * @brief  Gestisce le richieste dei vicini
 * @note   vedi tcp_comunication _loop
 * @param  socket_served:int la socket che richiede un servizio al peer
 * @retval int: il numero dei bytes letti , -1 in caso di errore
 */
int serve_peer(int socket_served){
    char *buffer;
    int bytes=0;
    uint64_t first_packet=0; // contiene la lunghezza del prossimo messaggio e del codice
    uint32_t len; // lunghezza messaggio
    uint32_t options;// 4 byte contenente una opzione per byte
    char operation; // Corrisponde al primo byte di options, 
                    // indica il tipo di operazione  da svolgere con 
                    // il prossimo pacchetto
    int peer_id,peer_port;
    struct in_addr peer_addr;
    if(recv(socket_served,(void*)&first_packet,sizeof(first_packet),0)<0){

        remove_neighbour(get_neighbour_by_socket(socket_served)->id);
        ds_option_set('x');//Richiedo di nuovo la neighbour list al DS, magari ho un nuovo amichetto
        return -1;
    }
    buffer = NULL;
    //4 byte di opzioni e 4byte che indica la lunghezza del pacchetto
    options = first_packet>>32;
    len = first_packet & PACKET_MASK;
    options = ntohl(options);
    len = ntohl(len);
    operation =(char) options; // ci serve il primo byte
    bytes = len+sizeof(uint64_t);
    my_log_print(peer_log,"Giunta operazione %c con lungezza %d\n",operation,len);
    if(len>0){
        my_log_print(peer_log,"Allocazione del buffer di lunghezza %d\n",len);
        buffer = malloc(sizeof(char)*len);
        my_log_print(peer_log,"In attesa del messaggio...\n",len);
        if(recv(socket_served,(void*)buffer,len,0)<0){
            my_log_print(peer_log,"Errore nella ricezione, gestisco...\n",len);
            return -1;
        }
        my_log_print(peer_log,"Messaggio letto correttamente\n");
    }
    switch(operation){
        case 'H': //Nuovo peer (Hello!)
            my_log_print(peer_log,"Gestione di una operazione 'Hello'\n");
            sscanf(buffer,"%d,%u,%d",&peer_id,&peer_addr.s_addr,&peer_port);
            my_log_print(peer_log,"Un nuovo peer si è unito al vicinato! id: %d addr: %u, porta: %d\n",peer_id,peer_addr.s_addr,peer_port);
            add_neighbour(peer_id,socket_served,peer_addr,peer_port);
            break;
        case 'U'://Update
            my_log_print(peer_log,"Gestione di un messaggio di backup da %d\n",socket_served);
            my_log_print(peer_log,"Messaggio ricevuto\n%s\n",buffer);
            manage_register_backup(buffer);
            //registers_list_print();
            break;
        case 'R'://Request
            my_log_print(peer_log,"Gestione di una Request ricevuta da %d\n",socket_served);
            my_log_print(peer_log,"Request : %s\n",buffer);
            manage_new_request(buffer,socket_served);
            //requestes_list_print();
            break;
        case 'r'://File Request
            my_log_print(peer_log,"Richiesta file da %d\n",socket_served);
            my_log_print(peer_log,"Messaggio ricevuto\n%s\n",buffer);
            manage_FILE_request(buffer,socket_served);
            break;
        case 'I'://Ignore
            my_log_print(peer_log,"Ricevuto 'Ignora' da %d\n",socket_served);
            my_log_print(peer_log,"Messaggio ricevuto\n%s\n",buffer);
            manage_ignore_answer(buffer,socket_served);
            break;
        case 'N'://Not found
            my_log_print(peer_log,"Ricevuto File Not Found da %d\n",socket_served);
            manage_FILE_not_found(socket_served);
            break;
        case 'A'://Answer
            my_log_print(peer_log,"Ricevuto una risposta da %d\n",socket_served);
            my_log_print(peer_log,"Messaggio ricevuto\n%s\n",buffer);
            manage_request_answer(buffer,socket_served);
            break;
        case 'F'://File Found
            my_log_print(peer_log,"Ricevuto un File da %d\n",socket_served);
            my_log_print(peer_log,"Messaggio ricevuto\n%s\n",buffer);
            manage_FILE_found(buffer,socket_served);
            break;
        case 'B'://bye bye
            my_log_print(peer_log,"Ricevuto un messaggio di addio da %d\n",socket_served);
            remove_neighbour(get_neighbour_by_socket(socket_served)->id);
            my_log_print(peer_log,"Richiedo al DS_thread la lista dei peer\n");
            ds_option_set('x');//Richiedo di nuovo la neighbour list al DS, magari ho un nuovo vicino
            //neighbors_list_print();
            break;
        default:
            //Il messaggio non rispetta i protocolli di comunicazione, rimuovo il peer
            my_log_print(peer_log,"Il peer non rispetta i protocolli di comunicazione ( chiusura irregolare? ), lo elimino dalla lista dei peer\n");
            remove_neighbour(get_neighbour_by_socket(socket_served)->id);
            my_log_print(peer_log,"Richiedo al DS_thread la lista dei peer\n");
            ds_option_set('x');//Richiedo di nuovo la neighbour list al DS, magari ho un nuovo vicino
            //neighbors_list_print();
            break;
    }
    if(buffer!=NULL){
        if(strlen(buffer)>0){
            free(buffer);
            buffer =NULL;
        }
    }
    return bytes; 
}
/**
 * @brief  Loop con il quale i peer comunicano fra loro
 * @note   
 * @param  *arg:void porta della server socket da aprire 
 * @retval 
 */
void * tcp_comunication_loop(void *arg){
    int peer_socket;
    int cur_bytes;
    unsigned int total_bytes =0;
    unsigned int packets_recived = 0;
    struct sockaddr_in s_addr;
    struct sockaddr_in peer_addr;
    int s_port = *((int*)arg);
    int ret;
    socklen_t len;
    struct timeval timeout;
    timeout.tv_usec = 0;
    my_log_print(peer_log,"Tentativo apertura socket alla porta %d\n",s_port);
    ret = open_tcp_server_socket(&s_socket,&s_addr,s_port);
    if(ret<0){
        perror("Errore nella costruzione della server socket\n");
        exit(-1);
    }
    ret = listen(s_socket,DEFAULT_SOCKET_SLOTS);
    printf("Socket tcp aperta alla porta: %d\n",s_port);
    my_log_print(peer_log,"Socket aperta con successo\n",s_port);
    FD_SET(s_socket,&master);
    pthread_mutex_lock(&fd_mutex);
    max_socket = s_socket;
    pthread_mutex_unlock(&fd_mutex);
    my_log_print(peer_log,"Tutto pronto, inizio loop\n");
    while(get_loop_flag()){
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
                    cur_bytes=serve_peer(peer_socket);
                }else{//Serviamo un peer;
                    my_log_print(peer_log,"Servo il client %d con Socket: %d\n",get_neighbour_by_socket(socket_i)->id,socket_i);
                    cur_bytes=serve_peer(socket_i);
                    
                    

                }
            }
        }
        if(cur_bytes>0){
            total_bytes+=cur_bytes;
            packets_recived+=2;
        }
    }
    my_log_print(peer_log,"Chiusura in corso...\n");
    my_log_print(peer_log,"Info traffico:\nBytes totali ricevuti %uB \nPacchetti ricevuti: %u\nMedia per pacchetto: %uB\n",total_bytes,packets_recived,(total_bytes/packets_recived));
    my_log_print(peer_log,"FINE\n");
    pthread_exit(NULL);
}


int main(int argc, char* argv[]){
    if(argc>1){
        my_port = atoi(argv[1]);
    }else{
        my_port = DEFAULT_PORT;
    }
    globals_init();
    if(pthread_create(&tcp_comunication_thread,NULL,tcp_comunication_loop,(void*)&my_port)){
        perror("Errore nella creazione del thread\n");
        exit(EXIT_FAILURE);
    }
    sleep(1);
    user_loop();
    globals_free();
    printf("Ciao, ciao!\n");
}
