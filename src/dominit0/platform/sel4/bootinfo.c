#include <sel4/sel4.h>
#include <sel4runtime.h>
#include <facetos/dominit0/klog.h>
#include <facetos/dominit0/kpanic.h>

seL4_BootInfo* platform_sel4_bi;

void platform_sel4_bootinfo_dump(const seL4_BootInfo *bi) {
	size_t untyped_count;

	if (bi == NULL) {
		klog(LOG_ERROR, "BootInfo: NULL\n");
		return;
	}

	klog_lock(); // we want all this as one output
	klog(LOG_DEBUG, "========== seL4 BootInfo ==========\n");
	klog(LOG_DEBUG, "BootInfo @ %p\n", bi);

	klog(LOG_DEBUG, "extraLen:       %llu\n",
	     (unsigned long long)bi->extraLen);

	klog(LOG_DEBUG, "nodeID:         %u\n",
	     (unsigned)bi->nodeID);

	klog(LOG_DEBUG, "numNodes:       %u\n",
	     (unsigned)bi->numNodes);

	klog(LOG_DEBUG, "numIOPTLevels:  %u\n",
	     (unsigned)bi->numIOPTLevels);

	klog(LOG_DEBUG, "ipcBuffer:      %p\n",
	     bi->ipcBuffer);

	klog(LOG_DEBUG, "\n--- CSpace slot regions ---\n");

	klog(LOG_DEBUG, "empty:            [%llu, %llu)\n",
	     (unsigned long long)bi->empty.start,
	     (unsigned long long)bi->empty.end);

	klog(LOG_DEBUG, "sharedFrames:     [%llu, %llu)\n",
	     (unsigned long long)bi->sharedFrames.start,
	     (unsigned long long)bi->sharedFrames.end);

	klog(LOG_DEBUG, "userImageFrames:  [%llu, %llu)\n",
	     (unsigned long long)bi->userImageFrames.start,
	     (unsigned long long)bi->userImageFrames.end);

	klog(LOG_DEBUG, "userImagePaging:  [%llu, %llu)\n",
	     (unsigned long long)bi->userImagePaging.start,
	     (unsigned long long)bi->userImagePaging.end);

	klog(LOG_DEBUG, "ioSpaceCaps:       [%llu, %llu)\n",
	     (unsigned long long)bi->ioSpaceCaps.start,
	     (unsigned long long)bi->ioSpaceCaps.end);

	klog(LOG_DEBUG, "extraBIPages:      [%llu, %llu)\n",
	     (unsigned long long)bi->extraBIPages.start,
	     (unsigned long long)bi->extraBIPages.end);

	klog(LOG_DEBUG, "untyped:           [%llu, %llu)\n",
	     (unsigned long long)bi->untyped.start,
	     (unsigned long long)bi->untyped.end);

	untyped_count = bi->untyped.end - bi->untyped.start;

	klog(LOG_DEBUG, "\n--- Untyped memory ---\n");
	klog(LOG_DEBUG, "count: %llu\n",
	     (unsigned long long)untyped_count);

	for (size_t i = 0; i < untyped_count; i++) {
		const seL4_UntypedDesc *desc = &bi->untypedList[i];
		seL4_CPtr cap = bi->untyped.start + i;

		klog(LOG_DEBUG,
		     "untyped[%llu]: cap=%llu paddr=0x%llx sizeBits=%u device=%u\n",
		     (unsigned long long)i,
		     (unsigned long long)cap,
		     (unsigned long long)desc->paddr,
		     (unsigned)desc->sizeBits,
		     (unsigned)desc->isDevice);
	}


	klog_unlock();
}

seL4_BootInfo* platform_sel4_get_bootinfo(void) {
     klog(LOG_DEBUG,"Grabbing seL4 BootInfo...\n");
     platform_sel4_bi = sel4runtime_bootinfo();

     if (platform_sel4_bi == NULL) kpanic("No seL4 BootInfo!");

     klog(LOG_INFO, "BootInfo @ %p\n", platform_sel4_bi);
     
     return platform_sel4_bi;
}
