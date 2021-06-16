#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdarg.h>
#include <time.h>

struct my_log* my_log_init(char *file_path,char *name);
void my_log_print(struct my_log* ml,const char* msg, ...);
void my_log_free(struct my_log *ml);
