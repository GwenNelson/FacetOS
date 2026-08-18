#include <facetos/dominit0/klog.h>
#include <sel4/sel4.h>

void main(int argc, char **argv, char **envp) {
     klog_init_early();
     klog(LOG_INFO,"Starting FacetOS...\n");
     klog_dump_debug();
     for (;;) {
        seL4_Yield();
     }
}
