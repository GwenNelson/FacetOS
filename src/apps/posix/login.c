#include <facetos/posix.h>
#include <stdlib.h>
#include <string.h>
#include <sys/wait.h>
#include <unistd.h>

static void read_line(char *buffer, unsigned capacity, int echo)
{
    unsigned length = 0;
    for (;;) { char c; if (read(0,&c,1)!=1) continue;
        if (c=='\r'||c=='\n') { buffer[length]=0; write(1,"\n",1); return; }
        if ((c==8||c==127)&&length) { length--; if(echo) write(1,"\b \b",3); continue; }
        if (c>=32&&c<127&&length+1<capacity) { buffer[length++]=c; if(echo) write(1,&c,1); }
    }
}
int main(void)
{
    uint64_t domain=get_domain_id(); char user[64], password[128];
    char digits[24]; unsigned count=0; do { digits[count++]=(char)('0'+domain%10); domain/=10; } while(domain);
    write(1,"FacetOS POSIX login (domain ",27);
    while(count) { char c=digits[--count]; write(1,&c,1); }
    write(1," on ",4);
    const char *terminal = getenv("FACET_TERMINAL");
    if (terminal != NULL) write(1, terminal, strlen(terminal));
    else write(1, "unknown terminal", 16);
    write(1,")\n",2);
    for (;;) { write(1,"login: ",7); read_line(user,sizeof(user),1); write(1,"password: ",10); read_line(password,sizeof(password),0);
        pid_t pid;
        int status;
        if(facet_posix_login_shell(user,password,"/bin/sh",&pid)!=0) { write(1,"Login incorrect\n",16); continue; }
        if (waitpid(pid,&status,0) != pid)
            write(1,"Unable to wait for shell\n",24);
    }
}
