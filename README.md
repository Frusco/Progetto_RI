# UNIPI A.A. 2020/2021 - DOCUMENTAZIONE PROGETTO RETI INFORMATICHE
Un’applicazione distribuita peer-to-peer che implementi un sistema per la
condivisione di dati costantemente aggiornati sulla pandemia di COVID-19. ( [link a tutte le specifiche di progetto](https://github.com/Frusco/Progetto_RI/blob/master/Documentazione/progetto2020-2021.pdf) )
## Comunicazione Peer - Discovery Server
Il Peer comunica periodicamente con il Discovery Server per ottenere la lista dei vicini e rinnovare la sua presenza all’interno della rete, inoltre DS sincronizza i registri aperti dei peer.
### Formato messaggio di richiesta ( Peer )
```<indirizzo>,<porta>,<data_registro_aperto>,<opzione>```
### Formato messaggio di risposta ( DS )
Se <opzione> è ```x```:
```<ID>,<numero_vicini>
<id_vicino>,<indirizzo>,<porta>
…
<id_vicino>,<indirizzo>,<porta>
```
Se <opzione> è ```r``` e la data non coincide:
```< data_registro_aperto >```

### Legenda:
- ```indirizzo```: indirizzo ip del peer.
- ```ID / id_vicino```: identificativo numerico che il DS associa al peer, nonché indice nella Peers Table dell’elemento.
- ```data_registro_aperto```: data trasmessa sia dai peer che dal DS per sincronizzare i registri aperti.
- ```opzione```: richiede diversi servizi al DS:
  - ```x``` : il peer richiede la lista dei vicini
  - ```r``` : riconferma la sua presenza in rete, aggiornando il time_to_live del peer ( refresh )
  - ```b```: messaggio di disconnessione dalla rete ( bye bye )
## Struttura per la gestione dei Peer nel Discovery Server
<p align="center">
  <img src="https://github.com/Frusco/Progetto_RI/blob/master/Documentazione/2.png?raw=true">
</p>
  
- ```Peers_list``` contiene una lista ordinata per porta di Peer Elem, il cui id è l’indice della Peers Table.
- Nella ```Peers Table``` sono raccolti tutti i peer descriptor che contengono le informazioni dei peer.
- ```neighbors_vector```contiene gli id dei vicini.
- ```Timer_List``` è una lista di ```Timer Elem``` che contengono il ```time_to_live``` dei peer presenti nella rete:
  - La lista è gestita in modo da sottrarre i secondi mancanti solo all’elemento in testa.
  - Quando il ```time_to_live``` scende a zero il peer viene considerato disconnesso e rimosso.
  
 Nota: la ```Peers Table``` e i vari ```neighbors_vector``` dei descrittori di peer sono vettori dinamici che vengono riallocati con il doppio della loro attuale dimensione via via che si riempiono. Gli indici ( ```id``` ) dei peer disconnessi vengono riassegnati ai nuovi peer e con essi il relativo spazio nella Peers Table.
## Comunicazione Peer - Peer
La comunicazione fra peer si basa sull’invio di due messaggi, uno di dimensione predefinita ( 8 Byte ) che identifica il successivo di dimensione e scopo variabile. Nel dettaglio:
| 3 Byte liberi | Tipo operazione ( 1 Byte ) | Dimensione mesaggio ( 4 Byte ) |
| ------------- |:--------------------------:| ------------------------------:|

### Tipi di operazione:
- ```H``` ( Hello! ): Nuovo peer
- ```U``` ( Update ): Backup di registri
- ```R``` ( Request ): Richiesta di elaborazione ;
- ```r``` ( File Request ): Richiesta di un File 
- ```I``` ( Ignore ): Richiesta di ignorare il peer nel conteggio del flooding
- ```N``` ( Not Found ): File richiesto non trovato dal peer
- ```A``` ( Answer ): Risposta di un peer in fase di flooding
- ```F``` ( File Found ): File richiesto trovato dal peer 
- ```B``` ( Bye Bye ): Avviso di disconnessione del peer.
### Gestione del flooding:
Il peer A che inizia un flooding invia una Request ai suoi vicini, quest’ultimi prima di rispondere inoltreranno la Request ai propri vicini ( ignorando la socket dalla quale l’hanno ricevuta ) e attendendo che tutti rispondano. Se un peer riceve un messaggio di flooding di una Request a cui sta già partecipando, risponderà con un messaggio Ignore, prevenendo così eventuali loop. Così facendo quando il peer A riceverà le risposte ( Answer ) dai suoi vicini saprà che il flooding è completo e potrà elaborare la Request.

## Strutture Dati e File dei registri e degli elaborati
<p align="center">
  <img src="https://github.com/Frusco/Progetto_RI/blob/master/Documentazione/1.png?raw=true">
</p>
  
- I registri sono salvati in File nominati ```<is_complete>_<is_open> - <creation_date>.txt``` 
  - Esempio: ```0_1-1624226400.txt``` è il registro aperto, non completo del 21/06/2021
- Gli elaborati sono salvati in File nominati ```<req_type><entry_type> - <date_start>_<date_end>.res```
  - Esempio ```tt-1623794400_1624140000.elab``` contiene il totale dei tamponi dal 16/06 al 20/06
## Consumi
### Discovery Server:
- In media DS riceve 59B ogni N secondi ( nel test 2 secondi ) da ogni peer collegato ( nel test 5 peers ). La media giornaliera è di circa 2.43MB a Peer con N = 2 secondi . Se ci fosse un peer per ogni comune italiano ( 7.903 ), il traffico giornaliero si aggirerebbe sui 18.7GB. È possibile abbattere il traffico incrementando N a scapito di una minore prontezza del server a riconoscere i peers che si scollegano in modo non corretto.
- In media DS invia 110B ogni qualvolta un peer chiede la lista aggiornata dei vicini, in media uno ogni 75 secondi. La media giornaliera si aggira quindi sui 123.7kB a Peer.
### Peer:
- I pacchetti scambiati tra peers in media si aggirano sugli 80B ciascuno inviati in media ogni 3 secondi, il traffico dipende molto dall’attività dei peers sui registri che raggiunge il suo culmine durante il flooding dei dati. Basandosi sui test si raggiungono i 2.19MB giornalieri a Peer in entrata e uscita.



