#include <facetos/dominit0/klog.h>
#include <facetos/dominit0/kmalloc.h>
#include <facetos/dominit0/kpanic.h>
#include <facetos/dominit0/platform/api.h>

void main(int argc, char **argv, char **envp) {
     platform_init_early();
     klog_init_early();
     klog(LOG_INFO,"Starting FacetOS...\n");
     klog_dump_debug();
     kmalloc_init_early();
     platform_init();

     for (;;) {
        platform_yield();
     }
}
