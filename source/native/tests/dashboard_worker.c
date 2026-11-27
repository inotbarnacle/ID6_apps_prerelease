#define _POSIX_C_SOURCE 200809L
#include <signal.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>
static volatile sig_atomic_t running=1;
static void stop(int ignored){(void)ignored;running=0;}
int main(int argc,char** argv){
    char path[1024];snprintf(path,sizeof path,"%s.log",argv[0]);
    signal(SIGTERM,stop);
    FILE* file=fopen(path,"a");if(!file)return 1;
    fprintf(file,"START %ld",(long)getpid());
    for(int i=1;i<argc;i++)fprintf(file," %s",argv[i]);
    fputc('\n',file);fflush(file);
    while(running)sleep(1);
    fputs("STOP\n",file);fclose(file);return 0;
}
