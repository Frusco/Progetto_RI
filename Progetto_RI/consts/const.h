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
#define SERVER_MAX_COMMAND_SIZE 13
#define SERVER_COMMANDS_NUMBER 4
#define SERVER_WELCOME_MSG "Discovery Server pronto, benvenuto!\nScrivi comando, ( help per mostrare lista comandi )\n"
#define SERVER_COMMAND_LIST {"help","showpeers","showneighbor","esc"}
#define SERVER_HELP_MSG "showpeers : mostra la lista dei peers ordinata per porta\nshowneighbor <ID> : mostra i vicini del peer richiesto\n esc: chiudi il server"