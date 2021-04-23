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

/*
### Neighbors struct ############################
*/
int my_id;
int my_port;
struct in_addr my_addr;
int loop_flag;
int max_socket;
fd_set master;
fd_set to_read;
pthread_mutex_t fd_mutex;
pthread_t ds_comunication_thread;
pthread_t comunication_thread;
struct neighbour{
    int id;
    struct sockaddr_in addr;
    int socket;
    struct neighbour* next;
};
pthread_mutex_t neighbors_list_mutex;
int neighbors_number;
struct neighbour* neighbors_list;

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


// inserimento in coda
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
 * @brief  Rimuove il vicino dalla lista restituendolo
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
        free(prev);
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
/**
 * @brief  Inizializza tutte le variabili globali condivise dai thread
 * @note   
 * @retval None
 */
void globals_init(){
    my_id =-1;
    my_port = -1;
    inet_pton(AF_INET,LOCAL_HOST,&my_addr);
    max_socket = 0;
    neighbors_number = 0;
    loop_flag = 1;
    ds_comunication_thread = 0;
    comunication_thread = 0;
    FD_ZERO(&master);
    FD_ZERO(&to_read);
    neighbors_list = NULL;
    pthread_mutex_init(&neighbors_list_mutex,NULL);
    pthread_mutex_init(&fd_mutex,NULL);
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
void* ds_comunication_loop(void*arg);
void user_loop(){
    //char msg[40];
    int port;
    int args_number;
    int command_index;
    char args[2][13];
    printf(PEER_WELCOME_MSG);
    while(loop_flag){
        printf(">> ");
        //fgets(msg, 40, stdin);
        args_number = scanf("%s %s",args[0],args[1]);
        // arg_len = my_parser(&args,msg);
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
                break;
            //__add__
            case 2:
                printf("to do\n");
            break;
            //__get__
            case 3:
                printf("to do\n");
                
            break;
            //__esc__
            case 4:
                loop_flag = 0;
                printf("Chiusura in corso...\n");
                sleep(1);
            break;
            //__comando non riconosciuto__
            default:
                printf("Comando non riconosciuto!\n");
            break;
        }

    }


}

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
unsigned int generate_greeting_message(struct neighbour* n,char** msg){
    char buffer[255];
    unsigned int len;
    if(n==NULL) return 0;           /*con il tuo localhost*/
    sprintf(buffer,"%d,%u,%d",my_id,my_addr.s_addr,my_port);
    len = sizeof(char)*strlen(buffer)+1;
    *msg = malloc(len);
    strcpy(*msg,buffer);
    return len;
}

int send_greeting_message(struct neighbour* n){
    char *buffer = NULL;
    uint64_t first_packet=0; // contiene la lunghezza del prossimo messaggio e del codice
    uint32_t len; // lunghezza messaggio
    uint32_t operations=0;
    char operation = 'H';//Hello!
    if(n == NULL) return 0; //Non dovrebbe MAI accadere
    /*GENERO IL MESSAGGIO E LO MANDO DOPO AVER MANDATO IL PRIMO PACCHETTO*/
    len = generate_greeting_message(n,&buffer);
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

/*
DS formato messaggio
<id>,<numero vicini>
<id_vicino>,<indirizzo>,<s_porta>
... (ripetuto per <numero vicini0>)
<id_vicino>,<indirizzo>,<s_porta>
*/



void update_neighbors(char*msg){
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

/*
DS formato messaggio
<id>,<numero vicini>
<id_vicino>,<indirizzo>,<s_porta>
... (ripetuto per <numero vicini0>)
<id_vicino>,<indirizzo>,<s_porta>
*/

void* ds_comunication_loop(void*arg){
    
    char buffer[DS_BUFFER];
    int socket,s_port,ds_port;
    socklen_t ds_addrlen;
    char option;
    struct sockaddr_in addr,ds_addr;
    inet_pton(AF_INET,LOCAL_HOST,&addr.sin_addr);
    s_port = MIN_PORT;
    //Apro una socket udp per riceve i messaggi da DS
    while(s_port<MAX_PORT){
        //Continuo a ciclare finché non trovo una s_porta libera oppure
        //Finisco le s_porte
        if(open_udp_socket(&socket,&addr,s_port)<0){
            if(s_port<MAX_PORT){
                s_port++;
                continue;
            }else{
                perror("Impossibile connettersi al Discovery Server");
                pthread_exit(NULL);
            }
        }
        break;
    }
    //Costruisco l'indirizzo al DS
    ds_port = *(int*)arg;
    printf("la s_porta è %d",ds_port);
    ds_addr.sin_family = AF_INET; //Tipo di socket
    ds_addr.sin_port = htons(ds_port);//Porta
    inet_pton(AF_INET,LOCAL_HOST,&ds_addr.sin_addr);
    ds_addrlen = sizeof(ds_addr);
    //option = (neighbors_number<2)?'x':'r';
    option = 'x';
    while(loop_flag){
        
        sprintf(buffer,"%u,%d,%c",addr.sin_addr.s_addr,my_port,option);
        sendto(socket,&buffer,strlen(buffer)+1,0,(struct sockaddr*)&ds_addr,ds_addrlen);
        if(option!='r'){
            if(recvfrom(socket,buffer,DS_BUFFER,0,(struct sockaddr*)&ds_addr,&ds_addrlen)<0){
                printf("Errore nella ricezione");
                continue;
            }
            printf("messaggio ricevuto:\n %s",buffer);
            update_neighbors(buffer);
        }
        option = 'r';
        sleep(5);
    }
    close(socket);
    pthread_exit(NULL);
}


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
    printf("Servo %d",socket_served);
    if(recv(socket_served,(void*)&first_packet,sizeof(first_packet),0)<0){
        perror("Errore in fase di ricezione\n");
        return;
    }
    //4 byte di opzioni e 4byte che indica la lunghezza del pacchetto
    options = first_packet>>32;
    len = first_packet & PACKET_MASK;
    options = ntohl(options);
    len = ntohl(len);
    printf("L'operazione è %d e la lunghezza è %d\n",options,len);
    operation =(char) options; // ci serve il primo byte
    printf("operation %c\n",operation);
    switch(operation){
        case 'H': //Nuovo peer (Hello!)
            printf("Servo un hallo!\n");
            buffer = malloc(sizeof(char)*len);
            if(recv(socket_served,(void*)buffer,len,0)<0){
                perror("Errore in fase di ricezione\n");
                return;
            }
            printf("Il messaggio di saluto %s\n",buffer);
            sscanf(buffer,"%d,%u,%d",&peer_id,&peer_addr.s_addr,&peer_port);
            printf("Quello che ho letto %d,%u,%d\n",peer_id,peer_addr.s_addr,peer_port);
            add_neighbour(peer_id,socket_served,peer_addr,peer_port);
            break;
    case 'P':
        break;
    case 2:
        break;
    default:
        return;
        break;
    }
    
}

void * tcp_comunication_loop(void *arg){
    
    int s_socket,peer_socket;
    struct sockaddr_in s_addr;
    struct sockaddr_in peer_addr;
    int s_port = *((int*)arg);
    int ret;
    socklen_t len;
    ret = open_tcp_server_socket(&s_socket,&s_addr,s_port);
    if(ret<0){
        perror("Errore nella costruzione della server socket\n");
        exit(-1);
    }
    ret = listen(s_socket,DEFAULT_SOCKET_SLOTS);
    printf("Server aperto alla porta: %d\n",s_port);
    
    FD_SET(s_socket,&master);
    printf("Server socket = %d\n",s_socket);
    pthread_mutex_lock(&fd_mutex);
    max_socket = s_socket;
    pthread_mutex_unlock(&fd_mutex);
    while(loop_flag){
        pthread_mutex_lock(&fd_mutex);
        to_read = master;
        pthread_mutex_unlock(&fd_mutex);
        printf("In attesa di una richiesta...\n");
        select(max_socket+1,&to_read,NULL,NULL,NULL);
        perror("select:");
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
        sleep(1);
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
    printf("%ld\n",comunication_thread);
    void* thread_ret;
    globals_init();
    if(argc>1){
        my_port = atoi(argv[1]);
    }else{
        my_port = DEFAULT_PORT;
    }
    if(pthread_create(&ds_comunication_thread,NULL,tcp_comunication_loop,(void*)&my_port)){
        perror("Errore nella creazione del thread\n");
        exit(EXIT_FAILURE);
    }
    sleep(1);
    user_loop();
    pthread_join(ds_comunication_thread,&thread_ret);
    globals_free();
    printf("Ciao, ciao!\n");
}
