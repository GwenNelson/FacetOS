/*
 * FacetOS initial task linker script
 *
 * Provides the TLS section boundary symbols expected by sel4runtime's
 * root-task startup code.
 *
 * TLS section layout derived from sel4runtime.
 */

SECTIONS
{
    /*
     * Initialised thread-local storage.
     *
     * sel4runtime uses these symbols to locate the initial TLS image.
     */
    .tdata :
    {
        _tdata_start = .;

        *(.tdata)
        *(.tdata.*)
        *(.gnu.linkonce.td.*)

        _tdata_end = .;
    }

    /*
     * Uninitialised thread-local storage.
     *
     * This occupies memory at runtime but does not need to occupy space
     * in the ELF file itself.
     */
    .tbss (NOLOAD) :
    {
        _tbss_start = .;

        *(.tbss)
        *(.tbss.*)
        *(.gnu.linkonce.tb.*)
        *(.tcommon)

        _tbss_end = .;
    }
}

/*
 * This is a linker-script fragment rather than a complete replacement
 * for GNU ld's normal x86-64 linker script. Keep the normal layout and
 * insert the TLS sections immediately after .rodata.
 */
INSERT AFTER .rodata;
