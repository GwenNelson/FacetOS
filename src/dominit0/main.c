#include <facetos/dominit0/klog.h>
#include <facetos/dominit0/kmalloc.h>
#include <sel4/sel4.h>

void main(int argc, char **argv, char **envp) {
     klog_init_early();
     klog(LOG_INFO,"Starting FacetOS...\n");
     klog_dump_debug();
     kmalloc_init_early();
     klog(LOG_DEBUG,"Testing an allocation...\n");
     void* my_buf = kmalloc(32);
     klog(LOG_DEBUG,"Allocated 32 bytes\n");
     kfree(my_buf);
     klog(LOG_DEBUG,"If we're still here, yay, it didn't crash\n");
     
     klog(LOG_DEBUG,"About to try a stupid large allocation, should panic...\n");
     kmalloc(2*1024*1024);
     klog(LOG_DEBUG,"If you see this, fuck\n");
     for (;;) {
        seL4_Yield();
     }
}
