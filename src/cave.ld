/* ported to the bitbox MK2 2.3.4 by tools/port_mk2.py */
/* Code cave: appended directly after the stock bitbox MK2 2.3.4 image (674896 bytes, ends at 0x080E4C50).
   The MK2 bootloader takes a longer image (vwal's 2.3.4+c ends at 0x080EA184 and runs); its upper limit is not known,
   so the cave is kept small and its size is reported by patch.py. */
MEMORY { CAVE (rx) : ORIGIN = 0x080E4C50, LENGTH = 0x1B000 }   /* up to 0x080FFC50: the flash bank ends at 0x08100000 */
SECTIONS {
  .cave : { *(.text .text.* .rodata .rodata.*) } > CAVE
  /* No RAM: the cave has no startup code, so initialised or zeroed globals would never be set up. */
  .ram (NOLOAD) : { *(.data .data.* .bss .bss.* COMMON) } > CAVE
  ASSERT(SIZEOF(.ram) == 0, "cave code must not use .data/.bss; put state in the filter object or at a fixed address")
  /DISCARD/ : { *(.ARM.exidx* .ARM.attributes .comment) }
}
