FacetOS TODO
============

BOOTSTRAP
---------

[ ] 1. Bootstrap allocator
    - Reserve a large static arena in .bss
    - Implement boot_alloc(size, align)
    - Monotonic allocation only; no free
    - Add basic bounds checking / panic on exhaustion
    - Must not depend on the real memory manager

[ ] 2. Parse seL4 BootInfo
    - Store the BootInfo pointer somewhere sensible
    - Enumerate all Untyped caps
    - Record:
        - CSlot / cap
        - physical address
        - sizeBits
        - device flag
    - Log every Untyped during development
    - Log total normal/device memory and Untyped count


CAPABILITY / PHYSICAL RESOURCE MANAGEMENT
-----------------------------------------

[ ] 3. Bootstrap CSpace allocator
    - Discover initially empty CSlots from BootInfo
    - Allocate/free CSlots
    - No dynamic CSpace expansion yet

[ ] 4. Untyped resource manager
    - Import all BootInfo Untypeds
    - Track normal vs device Untypeds
    - Track size/alignment
    - Allocate suitable Untyped resources
    - Retype/split resources as required
    - Track parent/provenance information
    - Support returning/reclaiming resources where possible

[ ] 5. seL4 kernel-object allocation primitives
    - Allocate destination CSlot
    - Select suitable Untyped
    - Retype into requested seL4 object type
    - Track resulting capability/object
    - Handle failure and cleanup correctly
    - Keep raw seL4 allocation machinery private to this layer


VIRTUAL MEMORY
--------------

[ ] 6. Physical frame allocator
    - Allocate seL4 Frame objects
    - Track physical backing/provenance
    - Free/reclaim frames

[ ] 7. VSpace manager
    - Create/manage page-table objects
    - Map frames
    - Unmap frames
    - Allocate kernel virtual-address ranges
    - Support creating an empty VSpace for a future process

[ ] 8. Real kernel heap
    - Implement kmalloc()
    - Implement kfree()
    - Obtain additional backing pages through VSpace/frame allocator
    - Keep bootstrap allocations permanently allocated
    - Switch kernel allocations from boot_alloc() to kmalloc()


FACETOS OBJECT MODEL
--------------------

[ ] 9. Define base object/interface ABI
    - UUID representation
    - QueryInterface()
    - Interface structs:
        void *self;
        function pointers...
    - Establish ABI conventions for methods/errors

[ ] 10. Define initial interfaces
    - Resource/object allocation
    - Frames / memory objects
    - Kernel objects
    - Process/task objects as needed

[ ] 11. IDL prototype
    - Define interface-description format
    - Generate C interface structs
    - Generate UUID constants
    - Later: generate IPC proxies and server dispatchers

[ ] 12. Wrap kernel resources as FacetOS objects
    - Keep seL4_CPtr and implementation details behind void *self
    - Expose resources through appropriate interfaces
    - Higher layers should stop manipulating raw seL4 caps unnecessarily


THREADS AND PROCESSES
---------------------

[ ] 13. Kernel thread support
    - Allocate TCB
    - Allocate stack
    - Configure registers/TLS
    - Start thread in root-task VSpace
    - Share root-task CSpace initially
    - Basic thread bookkeeping

[ ] 14. Process/task model
    - Process table owned by root task
    - PID allocation
    - Per-process state
    - Create independent VSpace
    - Create restricted CSpace
    - Create/configure initial TCB
    - Grant only explicitly required capabilities

[ ] 15. ELF loader
    - Parse ELF
    - Allocate/map load segments
    - Set permissions
    - Build initial stack
    - Start entry point in isolated process


BOOT / DRIVERS
--------------

[ ] 16. Initrd support
    - Locate initrd from boot information
    - Implement minimal archive reader
    - Find files by name
    - Feed executables into ELF loader

[ ] 17. Driver/service loader
    - Load drivers from initrd
    - Run each in isolated VSpace/CSpace
    - Grant device MMIO/IRQ capabilities explicitly
    - Provide interface-registration mechanism

[ ] 18. Generated cross-VSpace interfaces
    - Client-side interface structs use generated IPC functions
    - void *self points to proxy state
    - Marshal calls through seL4 IPC
    - Generate server-side dispatcher
    - Same source-level interface for local and remote objects


OS SERVICES
-----------

[ ] 19. Device/service registry
    - Register objects by supported interface UUIDs
    - Discover/query services
    - Support drivers appearing dynamically

[ ] 20. VFS / PolyFS foundations
    - File/object namespace
    - POSIX file operations as interfaces
    - Special files can expose arbitrary UUID interfaces
    - /dev etc. becomes a view onto FacetOS objects

[ ] 21. POSIX process personality
    - File descriptors
    - argv/env
    - fork/exec semantics or chosen equivalents
    - signals
    - process lifecycle/waiting
    - PID 1 / userspace init
