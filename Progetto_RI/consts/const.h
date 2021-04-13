//Sockets consts
#define DEFAULT_PORT 25565
#define MIN_PORT 1023
#define MAX_PORT 65535
#define DEFAULT_SOCKET_SLOTS 10
#define LOCAL_HOST "127.0.0.1"
// Discovery Server consts
#define MIN_NEIGHBOUR_NUMBER 2
#define DEFAULT_TABLE_SIZE 2
#define DEFAULT_NEIGHBOUR_VECTOR_SIZE 2
#define SERVER_MAX_COMMAND_SIZE 14
#define SERVER_COMMANDS_NUMBER 5
#define DS_BUFFER 255
#define SERVER_WELCOME_MSG "Discovery Server pronto, benvenuto!\nScrivi comando, ( help per mostrare lista comandi )\n"
#define SERVER_COMMAND_LIST {"help","showpeers","showneighbor","esc","showpeersinfo"}
#define SERVER_HELP_MSG "showpeers : mostra la lista dei peers ordinata per porta\nshowneighbor <ID> : mostra i vicini del peer richiesto\nshowpeersinfo : mostra nel dettaglio le informazioni di tutti i peers\nesc: chiudi il server\n"
#define DEFAULT_TIME_TO_LIVE 20
// Peer consts
#define DEFAULT_DS_PORT 4242
#define DEFAULT_PEER_PORT 25566
#define PEER_COMMANDS_NUMBER 3
#define PEER_MAX_COMMAND_SIZE 6
#define PEER_COMMAND_LIST {"start","add","get"}
