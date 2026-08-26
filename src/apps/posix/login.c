#include <facet_posix_runtime.h>
#include <facetos/interfaces/IPOSIXView.h>
#include <facetos/posix.h>
#include <stdlib.h>
#include <string.h>
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
    IPOSIXView *view=facet_posix_view(); uint64_t domain=0; char user[64], password[128];
    if (!view || view->get_domain_id(view->self,&domain)!=FACET_OK) return 1;
    char digits[24]; unsigned count=0; do { digits[count++]=(char)('0'+domain%10); domain/=10; } while(domain);
    write(1,"FacetOS POSIX login (domain ",27);
    while(count) { char c=digits[--count]; write(1,&c,1); }
    write(1," on ",4);
    const char *terminal = getenv("FACET_TERMINAL");
    if (terminal != NULL) write(1, terminal, strlen(terminal));
    else write(1, "unknown terminal", 16);
    write(1,")\n",2);
    for (;;) { write(1,"login: ",7); read_line(user,sizeof(user),1); write(1,"password: ",10); read_line(password,sizeof(password),0);
        FacetString u={.data=user,.length=strlen(user)}, p={.data=password,.length=strlen(password)}, shell={.data="/bin/sh",.length=7}, a=shell;
        FacetArray_string av={.data=&a,.count=1}; FacetHandle session={0}; int32_t e=0,pid=-1,status=0;
        if(view->authenticate(view->self,&u,&p,&session,&e)!=FACET_OK||e) { write(1,"Login incorrect\n",16); continue; }
        if(view->spawn_process(view->self,&shell,&av,session,&pid,&e)!=FACET_OK||e) { write(1,"Unable to start shell\n",22); continue; }
        while(view->wait_process(view->self,pid,&status,&e)==FACET_OK&&e) facet_posix_yield();
    }
}
