#include <facetos/dominit0/klog.h>
#include <facetos/dominit0/kmalloc.h>
#include <facetos/dominit0/kpanic.h>
#include <facetos/dominit0/platform/api.h>

#ifdef DEBUG
void test_kmalloc(void) {
     kmalloc_dump();
     klog(LOG_DEBUG,"test_kmalloc() - allocating 17kb buffer\n");
     void* buf_a = kmalloc(17 * 1024);
     klog(LOG_DEBUG,"test_kmalloc() - got buffer at %p\n",buf_a);
     kmalloc_dump();
     klog(LOG_DEBUG,"test_kmalloc() - allocation 13kb buffer\n");
     void* buf_b = kmalloc(13 * 1024);
     klog(LOG_DEBUG,"test_kmalloc() - got buffer at %p\n",buf_b);
     kmalloc_dump();
     klog(LOG_DEBUG,"test_kmalloc() - freeing both buffers...\n");
     kfree(buf_a);
     kfree(buf_b);
     kmalloc_dump();
}
#endif
void main(int argc, char **argv, char **envp) {
     platform_init_early();
     klog_init_early();
     klog(LOG_INFO,"Starting FacetOS...\n");

     #ifdef DEBUG
        klog_dump_debug();
     #endif

     kmalloc_init_early();
     #ifdef DEBUG
        test_kmalloc();
     #endif
     platform_init();
     klog_init_postboot();

     #ifdef DEBUG
        test_kmalloc();
     #endif

     for (;;) {
        platform_yield();
     }
}
