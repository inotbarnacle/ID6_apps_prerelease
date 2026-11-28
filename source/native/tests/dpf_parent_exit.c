#define _POSIX_C_SOURCE 200809L
#include <assert.h>
#include <poll.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/wait.h>
#include <unistd.h>
#ifdef __linux__
#include <sys/prctl.h>

/* An isolated guardian reaps the real worker after its immediate parent dies.
 * No process-global signal/subreaper state is changed in the Python runner. */
int main(int argc,char **argv) {
    int channel[2],status;
    pid_t worker,parent,guardian=getpid();
    assert(argc==4 && prctl(PR_SET_CHILD_SUBREAPER,1,0,0,0)==0 && pipe(channel)==0);
    parent=fork();assert(parent>=0);
    if (!parent) {
        close(channel[0]);
        assert(prctl(PR_SET_PDEATHSIG,SIGTERM)==0);
        if (getppid()!=guardian) _exit(1);
        worker=fork();assert(worker>=0);
        if (!worker) {
            close(channel[1]);
            execl(argv[1],argv[1],"--token","42","--host","127.0.0.1","--port",argv[2],
                  "--udp-port",argv[3],"--transport","hsfz",(char*)0);
            _exit(127);
        }
        assert(write(channel[1],&worker,sizeof worker)==(ssize_t)sizeof worker);close(channel[1]);
        for (;;) pause();
    }
    close(channel[1]);assert(read(channel[0],&worker,sizeof worker)==(ssize_t)sizeof worker);close(channel[0]);
    printf("%ld\n",(long)worker);fflush(stdout);
    (void)getchar();
    assert(kill(parent,SIGTERM)==0 && waitpid(parent,&status,0)==parent);
    for (unsigned i=0;i<500;++i) {
        const pid_t ended=waitpid(worker,&status,WNOHANG);
        if (ended==worker) {
            assert(WIFEXITED(status) && WEXITSTATUS(status)==0);
            puts("Production worker exited after parent death");return 0;
        }
        assert(ended==0);poll(0,0,10);
    }
    kill(worker,SIGKILL);waitpid(worker,&status,0);
    fputs("Worker did not exit after parent death\n",stderr);return 1;
}
#else
int main(void) { return 77; }
#endif
