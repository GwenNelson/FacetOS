#include <sel4/sel4.h>
#include <sel4/bootinfo.h>
#include <stdint.h>
#include <string.h>
#include <sel4/arch/bootinfo_types.h>
#include <sel4runtime.h>
#include <facetos/dominit0/klog.h>
#include <facetos/dominit0/kpanic.h>

seL4_BootInfo* platform_sel4_bi;

void platform_sel4_bootinfo_dump_mods(const seL4_BootInfo *bi) {
	const uint8_t *cur;
	const uint8_t *end;

	if (bi == NULL) {
		return;
	}

	cur = (const uint8_t *)bi + seL4_BootInfoFrameSize;
	end = cur + bi->extraLen;

	while ((size_t)(end - cur) >= sizeof(seL4_BootInfoHeader)) {
		seL4_BootInfoHeader header;

		/* Extended bootinfo chunks need not be naturally aligned. */
		memcpy(&header, cur, sizeof(header));
		if (header.len < sizeof(header) || header.len > (size_t)(end - cur)) {
			klog(LOG_ERROR, "Invalid extended BootInfo chunk length\n");
			return;
		}

		if (header.id == SEL4_BOOTINFO_HEADER_X86_MODULES) {
			seL4_X86_BootInfo_modules_t modules;
			size_t descriptor_offset = sizeof(modules);

			if (header.len < sizeof(modules)) {
				klog(LOG_ERROR, "Invalid multiboot module information\n");
				return;
			}

			memcpy(&modules, cur, sizeof(modules));
			if (modules.module_count >
			    (header.len - descriptor_offset) / sizeof(seL4_X86_BootInfo_module_t)) {
				klog(LOG_ERROR, "Invalid multiboot module count\n");
				return;
			}

			size_t names_offset = descriptor_offset +
				modules.module_count * sizeof(seL4_X86_BootInfo_module_t);

			for (seL4_Word i = 0; i < modules.module_count; i++) {
				seL4_X86_BootInfo_module_t desc;
				const char *name;
				size_t name_length;

				memcpy(&desc,
				       cur + descriptor_offset + i * sizeof(desc),
				       sizeof(desc));

				if (desc.name_offset < names_offset || desc.name_offset >= header.len) {
					klog(LOG_ERROR, "Invalid multiboot module name offset\n");
					return;
				}

				name = (const char *)cur + desc.name_offset;
				name_length = header.len - desc.name_offset;
				if (memchr(name, '\0', name_length) == NULL) {
					klog(LOG_ERROR, "Unterminated multiboot module name\n");
					return;
				}

				klog(LOG_INFO,
				     "Got module at %p of size %llu: %s\n",
				     (void *)(uintptr_t)desc.start,
				     (unsigned long long)desc.size,
				     name);
			}
			return;
		}

		cur += header.len;
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
