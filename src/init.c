#include <klog.h>
#include <sel4/sel4.h>

// temporary - we'll build a klibc or something later

/*char *strcpy(char *dest, const char *src)
{
    if (!src) {
        __asm__ volatile("ud2");
    }

    if (!dest) {
        __asm__ volatile("int3");
    }

    char *ret = dest;

    while ((*dest++ = *src++))
        ;

    return ret;
}*/

char *strcpy(char *dest, const char *src) {
    char *ret = dest;

    while ((*dest++ = *src++) != '\0')
        ;

    return ret;
}

void main(int argc, char **argv, char **envp) {
     klog_init_early();
     klog(LOG_INFO,"Starting FacetOS...\n");
     klog_lock();
     klog(LOG_DEBUG,"Testing FacetOS logging!\n");
     klog(LOG_DEBUG,"\t This should all be one output");
     klog_unlock();

     klog(LOG_INFO, "Hello %s: d=%d u=%u x=%x %%\n",  "FacetOS", -123, 456U, 0xdeadbeefU);
     for (;;) {
        seL4_Yield();
     }
}
