FacetOS TODO
============

This is the implementation roadmap from the current early dominit0 state to
the intended FacetOS architecture.

Design rule while working through this list:

    Capabilities determine authority.
    Configuration describes intended behaviour.

A domain is a resource/security abstraction, not necessarily a native FacetOS
userspace environment. Native FacetOS, POSIX personalities and VMs running
other operating systems can all be domains.

Items are ordered approximately by implementation dependency. Later sections
may be prototyped earlier where useful, but should not create dependencies
back into bootstrap code.


CURRENT BOOTSTRAP / TREE CLEANUP
================================

[ ] 1. Finish dominit0 source/build refactor
    - Keep the initial root executable named dominit0
    - Keep its sources under src/dominit0/
    - Build all direct src/dominit0/*.c translation units automatically
    - Keep klog/klock in dominit0 for now
    - Keep room to split reusable libraries/services out later
    - Ensure GRUB/ISO build installs and launches dominit0
    - Keep seL4-specific startup code identifiable so it can later become
      a bootstrap adapter rather than infecting generic domain code


EARLY BOOTSTRAP
===============

[ ] 2. Bootstrap allocator
    - Reserve a large static arena in .bss
    - Implement boot_alloc(size, align)
    - Monotonic allocation only; no real free
    - Add alignment and overflow/bounds checking
    - Panic cleanly on exhaustion
    - Must not depend on the real memory manager
    - Allow early permanent metadata to remain allocated indefinitely

[ ] 3. Parse seL4 BootInfo
    - Store/access the BootInfo pointer through a small bootstrap layer
    - Enumerate all Untyped caps
    - Record:
        - CSlot / cap
        - physical address
        - sizeBits
        - device flag
    - Log every Untyped during development
    - Log total normal/device memory and Untyped count
    - Inventory other resources needed to construct the initial domain
    - Avoid spreading direct seL4_BootInfo dependencies throughout dominit0

[ ] 4. Define generic FacetOS bootstrap/domain environment
    - Define the information generic dominit0 actually needs at startup
    - Separate:
        CONFIGURATION
            what this dominit0 is intended to construct/do
        ENVIRONMENT
            what resources/capabilities are actually available
    - Keep the environment independent of seL4 where practical
    - Implement an seL4 BootInfo -> FacetOS environment adapter
    - Leave room for another microkernel/bootstrap mechanism later
    - Keep human-readable command-line/config input where useful


CAPABILITY / PHYSICAL RESOURCE MANAGEMENT
=========================================

[ ] 5. Bootstrap CSpace allocator
    - Discover initially empty CSlots from BootInfo
    - Allocate/free CSlots
    - Track used/free slots robustly
    - No dynamic CSpace expansion yet

[ ] 6. Untyped resource manager
    - Import all BootInfo Untypeds
    - Track normal vs device Untypeds
    - Track physical range, size and alignment
    - Allocate suitable Untyped resources
    - Retype/split resources as required
    - Track parent/provenance information
    - Track allocation/retype state
    - Support returning/reclaiming resources where seL4 semantics permit
    - Keep allocator policy separable from seL4 retype/cap mechanics

[ ] 7. seL4 kernel-object allocation primitives
    - Allocate destination CSlot
    - Select suitable Untyped
    - Retype into requested seL4 object type
    - Track resulting capability/object
    - Track provenance needed for later cleanup/revocation
    - Handle partial failure and cleanup correctly
    - Keep raw seL4 allocation machinery private to this layer


VIRTUAL MEMORY
==============

[ ] 8. Physical frame allocator
    - Allocate seL4 Frame objects
    - Track physical backing/provenance
    - Support required frame sizes
    - Free/reclaim frames where possible

[ ] 9. VSpace manager
    - Create/manage page-table objects
    - Map frames
    - Unmap frames
    - Track mappings
    - Allocate/free virtual-address ranges
    - Support extending the dominit0 VSpace
    - Support creating an empty VSpace for a future process/domain

[ ] 10. Real kernel/dominit0 heap
    - Implement kmalloc()
    - Implement kfree()
    - Obtain additional backing pages through frame/VSpace allocators
    - Keep bootstrap allocations permanently allocated
    - Switch normal dominit0 allocations from boot_alloc() to kmalloc()
    - Keep allocation failure paths diagnosable


KERNEL LOGGING
==============

[ ] 11. Complete post-bootstrap klog
    - Preserve all early static-buffer log output
    - Keep early logging independent of dynamic allocation
    - Allocate larger dynamic log storage once kmalloc() is available
    - Copy/migrate early log into permanent storage
    - Implement proper ringbuffer/storage strategy
    - Allow sinks to be dynamically registered
    - Add VGA/console sink
    - Add useful log levels / prefixes / metadata
    - Keep logging usable while debugging memory allocation
    - Eventually expose retained log through a dmesg-like interface
    - Keep sink implementation independent enough to support later consoles


FACETOS OBJECT / INTERFACE MODEL
===============================

[ ] 12. Define UUID representation and conventions
    - Define stable UUID type/layout
    - Define naming conventions for interface UUIDs
    - Decide how UUID constants are declared/generated
    - Define comparison helpers

[ ] 13. Define base object / QueryInterface ABI
    - An interface contract is identified by UUID
    - QueryInterface(UUID) returns an interface instance
    - Define unsupported-interface/error behaviour
    - Do not require implementation-private object layouts

[ ] 14. Define local interface ABI
    - Interface structs begin with:
        void *self;
    - Remaining members are function pointers
    - Methods receive self explicitly
    - self is entirely private to the implementation
    - Same-VSpace calls should reduce to ordinary indirect function calls
    - Do not make callers branch on local-vs-remote transport

[ ] 15. Define initial core interfaces
    - Resource/object allocation
    - Frames / memory objects
    - Kernel objects
    - Process/task objects
    - Domain management
    - Logging/console where useful
    - Device/service discovery as needed
    - Keep interface contracts separate from implementation details

[ ] 16. Wrap low-level kernel resources as FacetOS objects/interfaces
    - Keep seL4_CPtr and implementation details behind void *self
    - Expose resources through appropriate interfaces
    - Higher layers should stop manipulating raw seL4 caps unnecessarily
    - Preserve seL4 capabilities as the underlying authority mechanism

[ ] 17. IDL prototype
    - Define a human-editable interface-description format
    - Generate interface UUID constants
    - Generate C interface structs
    - Generate method declarations/helpers
    - Preserve the void *self calling convention
    - Leave room for metadata/introspection

[ ] 18. Generated cross-VSpace interfaces
    - Generate client-side IPC proxy implementations
    - Proxy interface has the same source-level interface as a local object
    - void *self points to private proxy state
    - Marshal calls through seL4 IPC
    - Generate server-side dispatch/demarshalling
    - Support capability transfer where interface methods require it
    - Keep transport details out of normal callers

[ ] 19. Use the interface model for FacetOS servers themselves
    - Domain manager exposed through normal interfaces
    - Process manager exposed through normal interfaces
    - Filesystem/device services exposed through normal interfaces
    - Allow the same interface to be implemented locally or by an IPC proxy


THREADS AND PROCESSES
=====================

[ ] 20. Kernel/dominit0 thread support
    - Allocate TCB
    - Allocate stack
    - Configure registers/TLS
    - Start thread in dominit0/root-task VSpace
    - Share root-task CSpace initially
    - Basic thread bookkeeping
    - Establish per-thread IPC/TLS capability-slot conventions

[ ] 21. Process/task model
    - Process table owned by domain-management infrastructure
    - PID allocation
    - Per-process state
    - Thread ownership/lifecycle
    - Create independent VSpace
    - Create restricted CSpace
    - Create/configure initial TCB
    - Grant only explicitly required capabilities
    - Keep process identity distinct from domain identity

[ ] 22. ELF loader
    - Parse ELF
    - Allocate/map load segments
    - Apply permissions
    - Set up TLS where required
    - Build initial stack
    - Supply argc/argv/environment
    - Start entry point in isolated process


INITRD / PROGRAM / DRIVER LOADING
================================

[ ] 23. Initrd support
    - Locate initrd/module data from bootstrap information
    - Choose/implement a simple archive format reader
    - Find files by name
    - Expose file contents to the ELF loader
    - Keep initrd handling separate from eventual VFS implementation

[ ] 24. Driver/service metadata and loader
    - Describe required driver/service resources
    - Load executables from initrd
    - Run drivers/services in isolated VSpace/CSpace by default
    - Grant only required capabilities
    - Grant device MMIO/IRQ authority explicitly
    - Provide interface-registration mechanism
    - Avoid making drivers privileged merely because they are drivers

[ ] 25. Initial service startup ordering
    - Allow dominit0 configuration to specify initial services/drivers
    - Resolve dependencies through required/provided interfaces where useful
    - Avoid hard-coding the complete machine startup sequence into main()


DOMAIN ARCHITECTURE / DOMINIT0
=============================

[ ] 26. Maintain full domain architecture documentation
    - Define a domain as a resource/security/organizational abstraction
    - Make clear a domain is not synonymous with:
        - a process
        - a thread
        - a VSpace
        - a native FacetOS userspace
    - Document native FacetOS, POSIX and VM domain personalities
    - Document recursive/nested domain trees
    - Document configuration vs actual capability authority
    - Document that domain IDs are hierarchy-relative, not inherently global
    - Keep unresolved design questions explicitly marked as unresolved

[ ] 27. Define human-readable domain configuration
    - Initial domain-0 config can be loaded as a Multiboot module
    - Describe domain personality/type
    - Describe desired resources
    - Describe initial services/drivers/processes
    - Include policy hints such as:
        manage_domains=true/false
    - Keep format easy to edit by hand
    - Keep format extensible

[ ] 28. Define human-readable dominit0 startup environment
    - Describe resources/capabilities actually available
    - Allow CPtrs to be named by integer strings, e.g.:
        --domain-manager-cap=291
    - Make explicit that the integer does NOT transfer authority
    - Parent must first install/mint/copy the cap into the child's CSpace
    - The numeric value only identifies the cap in the child's CSpace
    - Include domain ID / hierarchy information where useful
    - Keep environment representation easy to inspect/debug
    - Move from argv to a human-readable environment/config file if it grows
      too large, rather than requiring an opaque binary format

[ ] 29. Define domain-management policy semantics
    - manage_domains=false is a policy/bootstrap hint, NOT a security boundary
    - Meaning is approximately:
        "you are not configured as the root manager of this hierarchy"
    - A modified dominit0 may ignore it
    - Any process may run dominit0 and construct its own nested domain tree
    - Any nested tree may call its own root "Domain 0"
    - This is intentional, analogous to nested virtualization
    - Security comes only from capabilities:
        a nested tree may subdivide/delegate authority it possesses
        it may not manufacture authority it was never given

[ ] 30. Implement native child-domain creation
    - Parent allocates resources for child
    - Create child CSpace
    - Create child VSpace
    - Create initial TCB/thread
    - Load/map dominit0
    - Install delegated capabilities into child CSpace
    - Construct child configuration/environment
    - Pass human-readable CPtr/resource information
    - Delegate only intended authority
    - Start child dominit0

[ ] 31. Implement recursive/nested native domains
    - Child dominit0 can construct children from authority it possesses
    - No special kernel recognition of "real" dominit0 is required
    - No globally privileged meaning of "Domain 0" is required
    - Confirm nested hierarchy remains confined by outer delegated caps
    - Test multiple independent hierarchies each containing a Domain 0

[ ] 32. Implement parent-proxied domain management
    - Define IDomainManager
    - Parent can expose it to a child through a badged/delegated IPC cap
    - Child receives a normal generated interface proxy
    - Parent decides which requested operations are permitted
    - Allow parent-mediated operations involving siblings/resources
    - Do not grant raw sibling authority merely to support such requests

[ ] 33. Dynamic CSpace/resource delegation
    - Transfer capabilities to already-running children over IPC
    - Allocate receive slots safely
    - Track delegated capability ownership/provenance
    - Support revocation/cleanup where possible
    - Grow CSpaces dynamically when bootstrap slot space is insufficient


DOMAIN PERSONALITIES
====================

[ ] 34. POSIX view of a native FacetOS domain
    - Present POSIX abstractions over the existing domain
    - Same underlying FacetOS resources/process world where intended
    - Provide dynamically generated read-only /etc
    - Define PID/process visibility semantics
    - Define filesystem/device views
    - Preserve access to native interfaces where explicitly requested

[ ] 35. Pure POSIX domain
    - Separate FacetOS domain whose primary personality is POSIX
    - Own process namespace/policy
    - Own generated/configured /etc
    - Run a POSIX PID 1
    - Support /sbin/init or chosen equivalent
    - Support getty/login where desired
    - Keep it implemented directly on FacetOS rather than requiring a VM

[ ] 36. Conventional inter-domain UNIX services
    - Allow POSIX/UNIX environments to consume FacetOS-provided services
    - Investigate/use conventional protocols where useful:
        - NFS
        - NIS
    - Keep this complementary to native FacetOS interfaces


VM DOMAINS
==========

[ ] 37. Define VM domain model
    - A VM running another OS is itself a domain personality/workload
    - dominit0 is not what makes something a domain
    - Parent allocates the VM domain's resources/authority
    - VM domain may contain no native FacetOS userspace beyond supporting VMM
    - Guest remains confined by the VM domain's delegated resources

[ ] 38. VMM foundation
    - Choose/integrate the required seL4 virtualization facilities
    - Create guest memory from delegated Frames/resources
    - Create/configure virtual CPUs
    - Provide virtual/assigned devices
    - Load and boot a guest kernel
    - Keep VMM privileges limited to the VM domain's authority

[ ] 39. OpenBSD VM domain prototype
    - Boot OpenBSD as a child domain workload
    - Provide suitable storage/console/network devices
    - Experiment with OpenBSD providing networking services to other domains
    - Export useful services through conventional protocols and/or adapters

[ ] 40. Linux VM domain prototype
    - Boot a minimal Linux guest as a child domain workload
    - Experiment with assigning/owning the physical GPU
    - Run Wayland/X11 or other graphics stack in the Linux VM domain
    - Export graphics/display services to other domains

[ ] 41. Nested virtualization/domain behaviour
    - Permit a VM guest to run nested VMs where hardware/platform permits
    - Treat nested guest hierarchies consistently with the recursive domain model
    - A nested FacetOS guest may call its own root Domain 0
    - Confirm outer capability/resource boundaries remain authoritative


DEVICE / SERVICE DISCOVERY
==========================

[ ] 42. Device/service registry
    - Register objects by supported interface UUIDs
    - Discover/query services by interface
    - Support services/drivers appearing dynamically
    - Avoid requiring one mandatory global registry of every object
    - Support protocol/interface availability notifications where useful

[ ] 43. Device object model
    - Represent discovered hardware as FacetOS objects
    - Allow one object to expose multiple interfaces
    - Drivers bind to capabilities/interfaces rather than concrete device names
    - Support child objects where hardware naturally exposes them
    - Keep UEFI-style protocol discovery ideas where they remain useful


VFS / POLYFS / OBJECT STORES
============================

[ ] 44. VFS foundations
    - Define namespace/mount model
    - Define file/directory operations as interfaces
    - Implement basic path lookup
    - Implement mount points
    - Support special files exposing arbitrary UUID interfaces
    - Make /dev and similar trees views onto FacetOS objects where appropriate

[ ] 45. POSIX file-descriptor layer
    - Map POSIX FDs onto FacetOS objects/interfaces
    - open/close/read/write/seek/etc.
    - Thread-safe FD tables
    - pipes
    - terminal/device descriptors
    - Preserve ability to obtain native interfaces from suitable special files

[ ] 46. PolyFS foundations
    - Implement intended FacetOS-native filesystem semantics
    - Integrate with VFS/POSIX views
    - Define storage and metadata model
    - Keep native object/interface functionality available through filesystem views

[ ] 47. Object-store core
    - Define StoreUUID and ObjectUUID semantics
    - ObjectUUID identifies an object within a store
    - Allow an object to expose multiple UUID-identified interfaces
    - Mount stores under a namespace such as:
        /objects/StoreUUID/ObjectUUID
    - Do not require one global registry containing every object
    - Define persistence and object lookup

[ ] 48. Object-store block-device backend
    - Store object stores on block devices
    - Define on-disk metadata/layout
    - Handle allocation/free space
    - Handle object metadata and interface/type information
    - Add recovery/consistency strategy

[ ] 49. Object-store -> filesystem adapters
    - Present selected object interfaces as conventional files/directories
    - Example:
        expose all IImage objects as files in a directory
    - Preserve underlying object identity where intended
    - Define write/mutation semantics

[ ] 50. Filesystem -> object-store adapters
    - Present ordinary files as FacetOS objects
    - Generic unknown file -> IByteStream
    - Define interfaces such as:
        ITextFile
        ITextStream
        IImage
    - Use file-type inspection/metadata to select useful interfaces
    - Define text encoding semantics
    - Define adapter identity, mutability and persistence semantics

[ ] 51. Generic adapter framework
    - Define when an adapter is a view vs a durable new object
    - Define identity across adapters
    - Define authority propagation
    - Define read/write mutation propagation
    - Support composing useful POSIX/native views without hidden privilege gain


FULL POSIX PERSONALITY
======================

[ ] 52. Complete POSIX process environment
    - argv/env
    - exec
    - fork or chosen equivalent
    - wait/process lifecycle
    - signals
    - process groups/sessions
    - credentials/permissions model
    - current working directory
    - environment variables
    - basic libc/syscall-facing ABI

[ ] 53. POSIX filesystem environment
    - /dev
    - /proc-like facilities if desired
    - generated/configured /etc
    - mounts
    - pipes/FIFOs
    - terminals/PTYs
    - enough semantics to run useful conventional software

[ ] 54. POSIX init/userland milestone
    - Boot pure POSIX domain into PID 1
    - Start services
    - Start getty/login
    - Obtain interactive shell
    - Run ordinary POSIX programs without FacetOS-specific knowledge


CONSOLES / MULTI-DOMAIN USER ENVIRONMENT
========================================

[ ] 55. Console/terminal service
    - Abstract physical/virtual consoles behind FacetOS interfaces
    - Support multiple consumers/domains
    - Integrate with klog where appropriate
    - Provide POSIX terminal/PTY views

[ ] 56. Virtual console switching
    - Support a configuration resembling:
        Alt-F1: Domain 0 native FacetOS shell
        Alt-F2: POSIX view of Domain 0
        Alt-F3: pure POSIX domain
        Alt-F4: OpenBSD VM domain
        Alt-F5: Linux graphics VM domain
    - Keep layout configurable; this is an example, not ABI

[ ] 57. Native FacetOS shell/login environment
    - Provide enough native object/interface tooling to inspect the system
    - Query objects/interfaces by UUID
    - Inspect domains/resources/processes
    - Launch native programs/services
    - Keep POSIX shell available separately through POSIX personality


ROBUSTNESS / SECURITY / LIFECYCLE
=================================

[ ] 58. Capability/authority audit
    - Verify every domain/process/service receives minimum required authority
    - Ensure config strings/IDs cannot create authority
    - Ensure remote interface proxies cannot exceed backing cap authority
    - Audit device/MMIO/IRQ delegation
    - Audit capability transfer paths

[ ] 59. Object/process/domain lifecycle
    - Define destruction semantics
    - Revoke delegated caps where appropriate
    - Stop threads/processes safely
    - Reclaim Frames/Untypeds where possible
    - Clean up IPC proxy/server state
    - Handle crashed services/domains

[ ] 60. Resource accounting
    - Account memory/kernel objects per process/domain
    - Track delegated resources through nested domain trees
    - Expose useful diagnostics
    - Allow policy/resource limits without confusing policy with authority

[ ] 61. SMP robustness
    - Audit klock and all shared allocators
    - Define lock ordering
    - Make CSpace/VSpace/process/object registries SMP-safe
    - Handle per-CPU/per-thread state
    - Avoid relying on UP-only bootstrap assumptions after SMP starts


DOCUMENTATION / PORTABILITY
===========================

[ ] 62. Keep architecture/design notes synchronized with implementation
    - Domain model and nesting
    - Capability authority model
    - Interface ABI
    - Bootstrap environment
    - Process model
    - VFS/object-store model
    - VM domains
    - POSIX personalities
    - Clearly mark unresolved decisions

[ ] 63. Document seL4 substrate boundary
    - Identify code that directly depends on:
        seL4_BootInfo
        seL4_CPtr
        Untyped_Retype
        seL4 IPC
        seL4 VSpace/TCB/CNode operations
    - Keep those dependencies behind FacetOS abstractions where sensible
    - Document what would need replacement for another microkernel

[ ] 64. Architecture/ABI documentation
    - Interface calling convention
    - UUID/IDL rules
    - Domain startup/config format
    - CSpace/bootstrap conventions
    - IPC wire ABI
    - Executable/process startup ABI
    - Object-store formats
    - Version compatibility rules


LATER / SIDE PROJECTS
=====================

[ ] 65. bootstub32
    - NOT on the FacetOS critical path
    - Investigate a small 32-bit Multiboot2 loader/stub
    - Make QEMU -kernel workflows convenient
    - Potentially make it useful as a standalone/upstream tool
    - Do not let this become a memory-manager-avoidance project


MAJOR MILESTONES
================

[M1] Self-hosted memory management
     boot_alloc -> Untyped/CSlot -> Frames -> VSpace -> kmalloc

[M2] Native isolation
     create TCB/VSpace/CSpace -> ELF loader -> isolated service

[M3] FacetOS object ABI
     QueryInterface -> local interfaces -> IDL -> remote IPC interfaces

[M4] Recursive domains
     parent dominit0 -> child dominit0 -> nested child tree

[M5] Useful native system
     drivers/services -> registry -> VFS -> native shell

[M6] POSIX environment
     POSIX view + pure POSIX domain -> PID 1 -> interactive userland

[M7] VM domains
     boot another OS as an ordinary child domain workload

[M8] Native object storage
     object store -> PolyFS/VFS adapters -> POSIX/native interoperability

[M9] Intended multi-domain machine
     native FacetOS + POSIX + OpenBSD/Linux VM domains cooperating through
     capabilities, interfaces and conventional protocols as appropriate
