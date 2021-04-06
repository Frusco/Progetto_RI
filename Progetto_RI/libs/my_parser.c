#include "my_parser.h"

/*
Utilizza in my_parser per costruire una lista di stringhe
*/
struct param{
    char* s;
    struct param* next;
};

/*
Parametri
char ***b : un puntatore ad un array di stringhe
char *source: stringa su cui effettuare il parsing
Return
int : Il numero delle stringhe divise da spazio
*/
int my_parser(char***b,char*source){
    int count = 0;
    char** buffer;
    char* copy_s;
    struct param *temp;
    struct param *head;
    char* s;
    copy_s = malloc(sizeof(char)*(strlen(source)+1));
    strcpy(copy_s,source);
    s = strtok(copy_s," ");
    temp = malloc(sizeof(struct param));
    head = temp;
    //Genero la lista di stringhe
    while(s!=NULL){
        if(s!= NULL){
            count++;
            temp->next = malloc(sizeof(struct param));
            temp->s = s;
            temp = temp->next;
            s = strtok(NULL," ");
        }else{
            temp->next = NULL;
        }
        
    }
    //Costruisco l'array di stringhe leggendo la lista e liberando memoria via via
    buffer = malloc(sizeof(char*)*count);
    for(int i = 0 ; i<count ;i++){
        buffer[i] = malloc((strlen(head->s)+1));
        strcpy(buffer[i],head->s);
        temp = head;
        head = head->next;
        free(temp);
    }
    *b = buffer;
    free(copy_s);
    return count;
}

/*void ugo(char**str){
    *str = malloc(sizeof(char)*5);
    memset(*str,'c',sizeof(char)*5);
}

int test(char***b){
    char** buffer;
    printf("inzio\n");
    buffer = malloc(sizeof(char*)*3);
    buffer[0] = malloc(sizeof(char)*(strlen("gatto")+1));
    strcpy(buffer[0],"gatto");
    buffer[1] = malloc(sizeof(char)*(strlen("gatto")+1));
    strcpy(buffer[1],"gatto");
    buffer[2] = malloc(sizeof(char)*(strlen("gatto")+1));
    strcpy(buffer[2],"gatto");
    printf("fine\n");
    *b = buffer;
    return 3;
}

int main(int argc, char* argv[]){
    char* str;
    strcpy(str,"Provo a fare cose");
    char** buffer;
    int quanti = my_parser(&buffer,str);
    //int quanti = 3;
    //quanti = test(&buffer);
    printf("Quanti è uguale a %d\n",quanti);
    for(int i = 0 ; i<quanti ; i++){
        printf("%s\n",buffer[i]);
    }
    //char *s;
    //ugo(&s);
    //printf("stringa %s\n",s);

}*/