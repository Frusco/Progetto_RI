//Sockets consts
#define DEFAULT_PORT 25565
#define MIN_PORT 1024
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
#define DEFAULT_TIME_TO_LIVE 5
// Peer consts
#define DEFAULT_DS_PORT 4242
#define DEFAULT_PEER_PORT 25566
#define PEER_COMMANDS_NUMBER 5
#define PEER_MAX_COMMAND_SIZE 6
#define PEER_WELCOME_MSG "Peer pronto, benvenuto!\nScrivi comando, ( help per mostrare lista comandi )\n"
#define PEER_HELP_MSG "start porta : \nadd : \nget :\nesc: chiudi il peer\n"
#define PEER_COMMAND_LIST {"help","start","add","get","esc"}
#define PACKET_MASK 0x00000000FFFFFFFF // <3 byte liberi><1 byte tipo operazione><4 byte lunghezza pacchetto>
#define DS_COMUNICATION_LOOP_SLEEP_TIME 2
#define DEFAULT_SELECT_WAIT_TIME 5;
