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
int loop_flag;
fd_set master;
fd_set reading;
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
        return n;
    }else{
        perror("Errore nell'apertura della neighbour socket");
        return NULL;
    }
}


struct neighbour* neighbour_init(int id,int socket,struct in_addr addr,int port){
    struct neighbour *n = malloc(sizeof(struct neighbour));
    n->id = id;
    n->next = NULL;
    n->socket = socket;
    memset(&n->addr,0,sizeof(n->addr)); //Pulizia
    n->addr.sin_family = AF_INET; //Tipo di socket
    n->addr.sin_port = htons(port);//Porta
    n->addr.sin_addr = addr;
    return n;
}


// inserimento in coda
void neighbors_list_add(struct neighbour* n){
    struct neighbour*cur,*prev;
    pthread_mutex_lock(&neighbors_list_mutex);
    cur = neighbors_list;
    while(cur){
        prev = cur;
        cur = cur->next;
    }
    prev->next = n;
    neighbors_number++;
    pthread_mutex_unlock(&neighbors_list_mutex);
}
// rimozione per id
int neighbors_list_remove(int id){
    pthread_mutex_lock(&neighbors_list_mutex);
    struct neighbour*cur,*prev=NULL;
    int ret = 0;
    cur = neighbors_list;
    while(cur){
        if(cur->id == id){
            break;
        }
        prev = cur;
        cur = cur->next;
    }
    if(cur && prev){
        prev->next = cur->next;
        free(cur);
        neighbors_number--;
        ret = 1;
    }else if(prev==NULL && cur!=NULL){
        neighbors_list = cur->next;
        free(cur);
        neighbors_number--;
        ret =1;
    }
    pthread_mutex_unlock(&neighbors_list_mutex);
    return ret;
}

struct neighbour* get_neighbour_by_id(int id){
    struct neighbour*cur;
    int ret = 0;
    pthread_mutex_lock(&neighbors_list_mutex);
    cur = neighbors_list;
    while(cur){
        if(cur->id == id){
            return cur;
        }
        cur = cur->next;
    }
    return NULL;
    pthread_mutex_unlock(&neighbors_list_mutex);
}


struct neighbour* add_neighbour(int id,int socket,struct in_addr addr,int port) {
    struct neighbour* n;   
    n = get_neighbour_by_id(id);
    if(n!=NULL) return n;//Già presente!
    if(socket==-1){//Ricevuto dal DS
        n = neighbour_init_and_connect(id,addr,port);
    }else{//Ricevuto dal socket listener
        n = neighbour_init(id,socket,addr,port);
    }
    if(n==NULL) return NULL;
    //Bisogna inserire la nuova socket nel master e aggiornare la socket più grande.
    //inserisci qua sotto;
    neighbors_list_add(n);
    return n;
}

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

/*void* neighbors_loop(int port){
    int ret,newfd,listener,addrlen,i,len,fdmax;

}*/


/*
### GLOBALS INIT E FREE #####################
*/
void globals_init(){
    my_id =-1;
    my_port = -1;
    neighbors_number = 0;
    loop_flag = 1;
    ds_comunication_thread = 0;
    comunication_thread = 0;
    FD_ZERO(&master);
    FD_ZERO(&reading);
    neighbors_list = NULL;
    pthread_mutex_init(&neighbors_list_mutex,NULL);
    pthread_mutex_init(&fd_mutex,NULL);
}

void globals_free(){
    neighbors_list_free();
}
/*
### USER INTERFACE ############################
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
    ds_addr.sin_port = htons(ds_port);//Porta
    inet_pton(AF_INET,LOCAL_HOST,&ds_addr.sin_addr);
    ds_addrlen = sizeof(ds_addr);
    sendto(socket,msg,5,0,(struct sockaddr*)&ds_addr,ds_addrlen);
    close(socket);
}
*/
void* ds_comunication_loop(void*arg);
void user_loop(){
    char msg[40];
    int port;
    int args_number;
    int command_index;
    char args[2][13];
    printf(PEER_WELCOME_MSG);
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
DS formato messaggio
<id>,<numero vicini>
<id_vicino>,<indirizzo>,<porta>
... (ripetuto per <numero vicini0>)
<id_vicino>,<indirizzo>,<porta>
*/



void update_neighbors(char*msg){
    char aux[100];
    struct neighbour *n;
    struct in_addr neig_addr;
    int id,port;
    id = port = -1;
    unsigned long index = 0;
    int entries_number;
    sscanf(msg,"%s\n",aux);
    sscanf(aux,"%d,%d",&my_id,&entries_number);
    index = strlen(aux);
    for(int i = 0; i<entries_number;i++){
        sscanf(msg+index,"%s\n",aux);
        printf("Leggo riga %s\n",aux);
        sscanf(aux,"%d,%u,%d",&id,&neig_addr.s_addr,&port);
        /*-1 perché non conosciamo la socket*/
        n = add_neighbour(id,-1,neig_addr,port);
        index += strlen(aux);
    }
}

/*
DS formato messaggio
<id>,<numero vicini>
<id_vicino>,<indirizzo>,<porta>
... (ripetuto per <numero vicini0>)
<id_vicino>,<indirizzo>,<porta>
*/

void* ds_comunication_loop(void*arg){
    printf("Sono il thread!\n");
    char buffer[DS_BUFFER];
    int socket,port,ds_port;
    socklen_t ds_addrlen;
    char option;
    struct sockaddr_in addr,ds_addr;
    inet_pton(AF_INET,LOCAL_HOST,&addr.sin_addr);
    port = MIN_PORT;
    //Apro una socket udp per riceve i messaggi da DS
    while(port<MAX_PORT){
        //Continuo a ciclare finché non trovo una porta libera oppure
        //Finisco le porte
        if(open_udp_socket(&socket,&addr,port)<0){
            if(port<MAX_PORT){
                port++;
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
    printf("la porta è %d",ds_port);
    ds_addr.sin_family = AF_INET; //Tipo di socket
    ds_addr.sin_port = htons(ds_port);//Porta
    inet_pton(AF_INET,LOCAL_HOST,&ds_addr.sin_addr);
    ds_addrlen = sizeof(ds_addr);
    printf("entro nel loopp\n");
    while(loop_flag){
        option = (neighbors_number<2)?'x':'r';
        sprintf(buffer,"%u,%d,%c",addr.sin_addr.s_addr,my_port,option);
        printf("Invio...\n");
        sendto(socket,&buffer,strlen(buffer)+1,0,(struct sockaddr*)&ds_addr,ds_addrlen);
        printf("Aspetto e ricevo\n");
        if(recvfrom(socket,buffer,DS_BUFFER,0,(struct sockaddr*)&ds_addr,&ds_addrlen)<0){
            continue;
        }
        if(option!='r'){
            update_neighbors(buffer);
        }
        sleep(5);
    }
    close(socket);
    pthread_exit(NULL);
}


int main(int argc, char* argv[]){
    
    printf("%ld\n",comunication_thread);
    void* thread_ret;
    globals_init();
    if(argc>1){
        my_port = atoi(argv[1]);
    }else{
        my_port = DEFAULT_PORT;
    }
    user_loop();
    pthread_join(ds_comunication_thread,&thread_ret);
    globals_free();
    printf("Ciao, ciao!\n");
}
