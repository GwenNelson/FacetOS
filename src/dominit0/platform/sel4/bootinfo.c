#include <sel4/sel4.h>
#include <sel4/bootinfo.h>
#include <stdint.h>
#include <sel4/arch/bootinfo_types.h>
#include <sel4runtime.h>
#include <facetos/dominit0/klog.h>
#include <facetos/dominit0/kpanic.h>

seL4_BootInfo* platform_sel4_bi;

void platform_sel4_bootinfo_dump_mods(const seL4_BootInfo *bi) {

	uint8_t *cur = (uint8_t *)bi + seL4_BootInfoFrameSize;
     uint8_t *end = cur + bi->extraLen;

  while (cur < end) {
      seL4_BootInfoHeader *header = (seL4_BootInfoHeader *)cur;

      if (header->len < sizeof(*header) || cur + header->len > end) {
          break;
      }

      if (header->id == SEL4_BOOTINFO_HEADER_X86_MODULES) {
          seL4_X86_BootInfo_modules_t *modules =
              (seL4_X86_BootInfo_modules_t *)cur;

	  seL4_X86_BootInfo_module_t *descs =
              (seL4_X86_BootInfo_module_t *)(modules + 1);

          for (seL4_Word i = 0; i < modules->module_count; i++) {
              const char *name =
                  (const char *)modules + descs[i].name_offset;

              klog(LOG_INFO,
                   "Got module at %p of size %llu: %s\n",
                   (void *)descs[i].start,
                   (unsigned long long)descs[i].size,
                   name);
          }
          break;
      }

      cur += header->len;
  }
}

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

        platform_sel4_bootinfo_dump_mods(bi);


	klog_unlock();
}

seL4_BootInfo* platform_sel4_get_bootinfo(void) {
     klog(LOG_DEBUG,"Grabbing seL4 BootInfo...\n");
     platform_sel4_bi = sel4runtime_bootinfo();

     if (platform_sel4_bi == NULL) kpanic("No seL4 BootInfo!");

     klog(LOG_INFO, "BootInfo @ %p\n", platform_sel4_bi);
     
     return platform_sel4_bi;
}
