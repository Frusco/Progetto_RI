// Discovery Server consts
#define MIN_NEIGHBOUR_NUMBER 2
//Dimensione di partenza della tabella dei descrittori di peer
#define DEFAULT_TABLE_SIZE 2
//Dimensione di partenza del vettore dei vicini di un peer
#define DEFAULT_NEIGHBOUR_VECTOR_SIZE 2
//La dimensione in caratteri del comando più grande
#define SERVER_MAX_COMMAND_SIZE 14
//Il numero dei comandi presenti
#define SERVER_COMMANDS_NUMBER 6
//Per la comunicazione UDP
#define DS_BUFFER 255
#define SERVER_WELCOME_MSG "Discovery Server pronto, benvenuto!\nScrivi comando, ( help per mostrare lista comandi )\n"
#define SERVER_COMMAND_LIST {"help","showpeers","showneighbor","esc","showpeersinfo","closeday"}
#define SERVER_HELP_MSG "showpeers : mostra la lista dei peers ordinata per porta\nshowneighbor <ID> : mostra i vicini del peer richiesto\nshowpeersinfo : mostra nel dettaglio le informazioni di tutti i peers\ncloseday: segnala ai peer di chiudere il registro di oggi\nesc: chiudi il server\n"
//Time to live dei peer ( in secondi )
#define DEFAULT_TIME_TO_LIVE 5
//Path che contiene la data del registro aperto
#define SYNC_TIME_PATH "ds_sync_time.txt"
//Ore e minuti dell'orario di chiusura del registro del giorno
#define END_REG_HOUR 15
#define END_REG_MINUTES 32

//Nomi e path dei file di log
#define DEFAULT_TIMER_COMUNICATION_LOG_FILENAME "ds_timer.log"
#define DEFAULT_DS_COMUNITCATION_LOG_FILENAME "ds_comunication.log"
#define DEFAULT_USER_LOG_FILENAME "ds_user.log"
#define DEFAULT_DS_THREAD_NAME "DS_Thread"
#define DEFAULT_TIMER_THREAD_NAME "TIMER_Thread"
#define DEFAULT_USER_THREAD_NAME "USER_Thread"