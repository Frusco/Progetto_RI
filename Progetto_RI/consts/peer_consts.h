// Peer consts
#define DEFAULT_DS_PORT 4242
#define DEFAULT_PEER_PORT 25566
//Numero di comandi e lunghezza del comando più grande
#define PEER_COMMANDS_NUMBER 7
#define PEER_MAX_COMMAND_SIZE 10
//Buffer per la comunicazione con il DS tramite UDP
#define DS_BUFFER 255
#define PEER_WELCOME_MSG "Peer pronto, benvenuto!\nScrivi comando, ( help per mostrare lista comandi )\n"
#define PEER_HELP_MSG "start <porta> : Apre la comunicazione con il DS\nadd <t,n> <numero>: aggiunge <numero> tamponi(t) positivi(n) nel registro aperto\nget <request_type (t/v) > <entry_type (t/n) > <dd1:mm1:yyy1>,<dd2:mm2:yyyy2>: Genera ed elabora la richiesta\nshowpeers: mostra i vicini\nshowregs: mostra i registri salvati in locale\nesc: chiudi il peer\n"
#define PEER_COMMAND_LIST {"help","start","add","get","esc","showpeers","showregs"}
//Maschera del primo pacchetto da 8 byte fissi
// < 3 byte liberi> < 1 byte tipo operazione > < 4 byte lunghezza prossimo pacchetto >
#define PACKET_MASK 0x00000000FFFFFFFF 
//Sleep time tra l'invio di un messaggio e l'altro al DS
//Coinvolge anche i messaggi di refresh di presenza del peer sulla rete al DS
#define DS_COMUNICATION_LOOP_SLEEP_TIME 2
//Wait time della select
#define DEFAULT_SELECT_WAIT_TIME 5;
//Path e nomi dei file di log
#define DEFAULT_PEER_COMUNICATION_LOG_FILENAME "/log/peers.log"
#define DEFAULT_DS_COMUNITCATION_LOG_FILENAME "/log/ds_comunication.log"
#define DEFAULT_USER_LOG_FILENAME "/log/user.log"
#define DEFAULT_DS_THREAD_NAME "DS_Thread"
#define DEFAULT_PEER_THREAD_NAME "PEER_Thread"
#define DEFAULT_USER_THREAD_NAME "USER_Thread"