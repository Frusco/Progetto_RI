# UNIPI A.A. 2020/2021 - DOCUMENTAZIONE PROGETTO RETI INFORMATICHE
Un’applicazione distribuita peer-to-peer che implementi un sistema per la
condivisione di dati costantemente aggiornati sulla pandemia di COVID-19.
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

###Legenda:
- ```indirizzo```: indirizzo ip del peer.
- ```ID / id_vicino```: identificativo numerico che il DS associa al peer, nonché indice nella Peers Table dell’elemento.
- ```data_registro_aperto```: data trasmessa sia dai peer che dal DS per sincronizzare i registri aperti.
- ```opzione```: richiede diversi servizi al DS:
  - ```x``` : il peer richiede la lista dei vicini
  - ```r``` : riconferma la sua presenza in rete, aggiornando il time_to_live del peer ( refresh )
  - ```b```: messaggio di disconnessione dalla rete ( bye bye )
## Struttura per la gestione dei Peer nel Discovery Server

