#include "my_logger.h"

struct my_log{
    char* name;
    char* file_path;
};
/**
 * @brief  Inizializza una struttura my_log
 * @note   
 * @param  *fp:char il path con il nome del file dove salvare il log 
 * @param  *n:char il nome che sarà visualizzato nel log ( es: thread1 oppure DS_Server ) 
 * @retval Puntatore my_log* alla struttura allocata
 */
struct my_log* my_log_init(char *fp,char *n){
    struct my_log* ml;
    printf("Creazione di un nuovo file di log %s - %s\n",fp,n);
    if(fp==NULL){
        printf("fp NULL");
        return NULL;
    }
    if(strlen(fp)==0){
        printf("fp stringa vuota\n");
        return NULL;
    }
    if(n==NULL){
        printf("Nome NUllo\n");
        return NULL;
    }
    if(strlen(n)==0)return NULL;
    ml = malloc(sizeof(struct my_log));
    ml->file_path = malloc(strlen(fp)+1);
    strcpy(ml->file_path,fp);
    ml->name = malloc(strlen(n)+1);
    strcpy(ml->name,n);
    return ml;
}
/**
 * @brief  Stampa su file la stringa
 * @note   Non è thread safe
 * @param  *ml:my_log struttura che definisce dove stampare la stringa 
 * @param  *msg:char il messaggio 
 * @retval None
 */
void my_log_print(struct my_log *ml,const char *msg, ...){
    FILE *f;
    time_t t;
    struct tm *tt;
    va_list args;
    if(ml==NULL)return;
    if(msg==NULL)return;
    time(&t);
    tt = localtime(&t);
    f = fopen(ml->file_path,"a");
    fprintf(f,
            "%d-%d-%d %d:%d:%d >> [%s]: ",
            tt->tm_year+1900,
            tt->tm_mon+1,
            tt->tm_mday,
            tt->tm_hour,
            tt->tm_min,
            tt->tm_sec,
            ml->name
    );
    //printf("Provo a stampare %s\n",msg);
    va_start (args, msg);
    //printf("vfprint\n");
    vfprintf (f, msg, args);
    //printf("va_end\n");
    va_end (args);
    //printf("Stampato tutto ciao ciao\n");
    fclose(f);
}

void my_log_free(struct my_log *ml){
    free(ml->file_path);
    free(ml->name);
    free(ml);
}