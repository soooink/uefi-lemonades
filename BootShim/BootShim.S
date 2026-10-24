/* Derived from uefi-sheng 329b473d BootShim.S; umi-only RAM trace.
 * LEM0: shim reached; LEM1: FD copied. x0/x1 handoff is preserved.
 * Only the reserved console header/data are changed; no pmsg or disk I/O.
 */
_Head:
  adr   x1, _Payload    /* Set _Entry address */
  b     _Start          /* Jump to the real code */

#if REQUIRES_KERNEL_HEADER == 1
_StackBase:
  /* The OnePlus 8T-family ABL parses this header like a Linux arm64 Image:
     text_offset is the offset from the START OF RAM, so FD_BASE here would
     send the load far above the end of DRAM and the ABL refuses silently.
     Keep 0 + "place anywhere" (flags 0xa), like the verified ios-topaz probe. */
  .quad 0               /* Text Offset */

_StackSize:
  .quad FD_SIZE + (_Payload - _Head) /* Complete shim and payload */
  .quad 0xa             /* Flags: LE, place anywhere */
  .quad 0               /* Reserved */
  .quad 0               /* Reserved */
  .quad 0               /* Reserved */
  .ascii "ARM\x64"      /* ARM64 Magic */
  .long 0               /* Reserved */
#endif

_Start:
  /* lemonades ramoops console header: confirmed in this kernel, ECC disabled. */
  ldr   x12, =0xB0000000
  ldr   w13, =0x43474244
  str   w13, [x12]
  ldr   w13, =0x304d454c
  str   w13, [x12, #12]
  mov   w13, #10
  strb  w13, [x12, #16]
  mov   w13, #5
  str   w13, [x12, #4]
  str   w13, [x12, #8]
  dmb   sy
  dc    cvac, x12
  dsb   sy
  /* Stage 1: paint the inherited splash framebuffer RED (shim entered).
     ABL hands off with MMU and D-cache off, so stores reach DDR and the
     panel self-refresh shows them at once (verified by the ios-topaz probe
     console on this handset). 1080*2400*4 = 0x9E5000 bytes at 0x9C000000. */
  ldr   x13, =0x00FF0000
  ldr   x14, =0x9C000000
  ldr   x15, =0x9E5000
1:
  str   w13, [x14], #4
  subs  x15, x15, #4
  b.ne  1b
  /* Reset the on-screen console cursor for this boot. */
  ldr   x14, =0xB0201000
  str   wzr, [x14]
  str   wzr, [x14, #4]
  mov   x4, x1
  ldr   x5, =FD_BASE
  cmp   x4, x5
  beq   _Entry
  ldr   x6,  =FD_SIZE

_CopyLoop:
  ldp   x2, x3, [x4], #16
  stp   x2, x3, [x5], #16
  subs  x6, x6, #16
  b.ne  _CopyLoop
  ldr   x5, =FD_BASE

_Entry:
  /* lemonades ramoops console header: confirmed in this kernel, ECC disabled. */
  ldr   x12, =0xB0000000
  ldr   w13, =0x43474244
  str   w13, [x12]
  ldr   w13, =0x314d454c
  str   w13, [x12, #12]
  mov   w13, #10
  strb  w13, [x12, #16]
  mov   w13, #5
  str   w13, [x12, #4]
  str   w13, [x12, #8]
  dmb   sy
  dc    cvac, x12
  dsb   sy
  /* Stage 2: paint GREEN - the firmware volume is copied to the FD region
     and the UEFI SEC entry is next. Green + nothing further = SEC died
     before its first DEBUG write. */
  ldr   x13, =0x0000FF00
  ldr   x14, =0x9C000000
  ldr   x15, =0x9E5000
2:
  str   w13, [x14], #4
  subs  x15, x15, #4
  b.ne  2b
  /* Publish the copied instructions before executing them. */
  dsb   sy
  mrs   x9, ctr_el0
  ubfx  x9, x9, #16, #4
  mov   x10, #4
  lsl   x9, x10, x9
  sub   x10, x9, #1
  bic   x8, x5, x10
  ldr   x7, =FD_SIZE
  add   x7, x5, x7

_CleanLoop:
  dc    cvac, x8
  add   x8, x8, x9
  cmp   x8, x7
  b.lo  _CleanLoop
  dsb   sy
  ic    iallu
  dsb   sy
  isb
  br    x5

_Dead:
  b     _Dead

.text
.ltorg
.align 4

_Payload:
