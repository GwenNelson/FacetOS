FacetOS TODO
============

BOOTSTRAP
---------

[ ] 1. Bootstrap allocator
    - Reserve a large static arena in .bss
    - Implement boot_alloc(size, align)
    - Monotonic allocation only; no free
    - Add bounds checking / panic on exhaustion
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


KERNEL LOGGING
--------------

[ ] 9. Complete post-bootstrap klog
    - Preserve all early static-buffer log output
    - Allocate larger dynamic log storage once kmalloc() is available
    - Copy/migrate early log into permanent log
    - Implement proper ringbuffer/storage strategy
    - Allow sinks to be dynamically registered
    - Add VGA/console sink
    - Add log levels / prefixes / metadata
    - Keep logging usable while debugging memory allocation
    - Eventually expose retained log through dmesg-like interface


FACETOS OBJECT MODEL
--------------------

[ ] 10. Define base object/interface ABI
    - UUID representation
    - QueryInterface()
    - Interface structs:
        void *self;
        function pointers...
    - Establish ABI conventions for methods/errors

[ ] 11. Define initial interfaces
    - Resource/object allocation
    - Frames / memory objects
    - Kernel objects
    - Process/task objects as needed

[ ] 12. IDL prototype
    - Define interface-description format
    - Generate C interface structs
    - Generate UUID constants
    - Later: generate IPC proxies and server dispatchers

[ ] 13. Wrap kernel resources as FacetOS objects
    - Keep seL4_CPtr and implementation details behind void *self
    - Expose resources through appropriate interfaces
    - Higher layers should stop manipulating raw seL4 caps unnecessarily


THREADS AND PROCESSES
---------------------

[ ] 14. Kernel thread support
    - Allocate TCB
    - Allocate stack
    - Configure registers/TLS
    - Start thread in domain root-task VSpace
    - Share root-task CSpace initially
    - Basic thread bookkeeping

[ ] 15. Process/task model
    - Process table owned by domain root task
    - PID allocation
    - Per-process state
    - Create independent VSpace
    - Create restricted CSpace
    - Create/configure initial TCB
    - Grant only explicitly required capabilities

[ ] 16. ELF loader
    - Parse ELF
    - Allocate/map load segments
    - Set permissions
    - Build initial stack
    - Start entry point in isolated process


BOOT / DRIVERS
--------------

[ ] 17. Initrd support
    - Locate initrd from boot information
    - Implement minimal archive reader
    - Find files by name
    - Feed executables into ELF loader

[ ] 18. Driver/service loader
    - Load drivers from initrd
    - Run each in isolated VSpace/CSpace
    - Grant device MMIO/IRQ capabilities explicitly
    - Provide interface-registration mechanism

[ ] 19. Generated cross-VSpace interfaces
    - Client-side interface structs use generated IPC functions
    - void *self points to proxy state
    - Marshal calls through seL4 IPC
    - Generate server-side dispatcher
    - Same source-level interface for local and remote objects


DOMAIN ARCHITECTURE
-------------------

[ ] 20. Document FacetOS domain design
    - Define a "domain" precisely
    - Define responsibilities of the domain root task
    - Document relationship between:
        - seL4
        - domains
        - dominit0
        - processes
        - subdomains
        - capabilities
        - VSpaces / CSpaces
    - Document domain authority and inheritance rules

    Initial model:

        seL4
          |
          +-- dominit0 (domain 0)
                |
                +-- processes
                |
                +-- child domain
                |     |
                |     +-- dominit0
                |     +-- processes
                |
                +-- child domain
                      |
                      +-- dominit0
                      +-- processes

[ ] 21. Refactor root-task build around dominit0
    - Stop treating the root executable as generically "init"
    - Build the domain initialiser as something like:
        dominit0
    - Keep dominit0 usable both as:
        - initial seL4 root task
        - root task of a child FacetOS domain
    - Separate domain bootstrap code from seL4-specific initial bootstrap
      where necessary

[ ] 22. Define domain configuration format
    - Configuration supplied to initial dominit0 as a Multiboot module
    - Describe what kind of domain to construct
    - Initial options/concepts should include:
        - pure FacetOS domain
        - personality/type of domain
        - whether this domain may manage subdomains
        - resources/capabilities assigned to the domain
        - initial services/drivers/processes
    - Keep format extensible

[ ] 23. Define dominit0 environment
    - A child dominit0 receives an environment from its parent domain
    - Environment describes authority actually delegated to it
    - Support at least:
        - "you do not manage domains"
        - "you manage domains directly"
        - "you manage domains through this capability/interface"
    - Include inherited resource/capability information
    - Do not assume child domain possesses the same authority as domain 0
    - Configuration expresses desired setup;
      environment expresses authority/resources actually available

[ ] 24. Implement domain creation
    - Parent domain allocates resources for child
    - Create child VSpace
    - Create child CSpace
    - Create initial TCB
    - Load dominit0
    - Supply domain configuration
    - Construct/pass dominit0 environment
    - Delegate only required capabilities
    - Start child domain

[ ] 25. Subdomain management
    - Allow a domain with appropriate authority to create/destroy children
    - Support direct domain-management authority
    - Support delegated/proxied management through an interface capability
    - Ensure domains without management authority cannot create children


OS SERVICES
-----------

[ ] 26. Device/service registry
    - Register objects by supported interface UUIDs
    - Discover/query services
    - Support drivers appearing dynamically

[ ] 27. VFS / PolyFS foundations
    - File/object namespace
    - POSIX file operations as interfaces
    - Special files can expose arbitrary UUID interfaces
    - /dev etc. becomes a view onto FacetOS objects

[ ] 28. POSIX process personality
    - File descriptors
    - argv/env
    - exec
    - fork or chosen equivalent
    - signals
    - process lifecycle/waiting
    - userspace init
