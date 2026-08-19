#include <facetos/dominit0/klog.h>
#include <facetos/dominit0/kmalloc.h>
#include <facetos/dominit0/kpanic.h>
#include <facetos/dominit0/platform/api.h>

void test_kmalloc(void) {
#ifdef DEBUG
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
#endif
}

void main(int argc, char **argv, char **envp) {
     platform_init_early();
     klog_init_early();
     klog(LOG_INFO,"Starting FacetOS...\n");
     klog_dump_debug();
     kmalloc_init_early();
     test_kmalloc();
     platform_init();

     test_kmalloc();

     for (;;) {
        platform_yield();
     }
}
