/** @file
  Single-core lemonades RAM console compatible with this handset's Linux ramoops.

  SPDX-License-Identifier: BSD-2-Clause-Patent

  Board evidence: LineageOS android_kernel_xiaomi_sm8250, commit
  71b13e62f057a649b77fe4062feb73ee72ad609c:
  https://github.com/LineageOS/android_kernel_xiaomi_sm8250/blob/71b13e62f057a649b77fe4062feb73ee72ad609c/fs/pstore/ram.c#L983-L1006
  fixes the base at 0xB0000000 and splits ramoops_memreserve=4M equally
  between console and pmsg. ram_core.c defines the 12-byte DBGC header,
  ring layout and signature XOR. The running handset reports this base,
  size, record/ftrace sizes 0 and ECC 0/0. Pmsg is never accessed here.

  The entire ramoops window must be reserved and mapped as uncached normal
  memory by this board. At entry the data cache must be off; after MMU setup
  the mapping must remain uncached. These barriers do not flush cached data.
  No SMP locking, allocation, Boot Services or DEBUG calls are used.
**/

#include <Base.h>
#include <Library/BaseLib.h>
#include <Library/SerialPortLib.h>

#define LEMONADES_RAMOOPS_CONSOLE_BASE  0xB0000000ULL
#define LEMONADES_RAMOOPS_CONSOLE_SIZE  0x00200000U
#define LEMONADES_RAMOOPS_PMSG_BASE     0xB0200000ULL
#define LEMONADES_RAMOOPS_SIGNATURE     0x43474244U

typedef struct {
  UINT32  Signature;
  UINT32  Start;
  UINT32  Size;
} LEMONADES_RAMOOPS_HEADER;

#define LEMONADES_RAMOOPS_CAPACITY  (LEMONADES_RAMOOPS_CONSOLE_SIZE - sizeof (LEMONADES_RAMOOPS_HEADER))

STATIC_ASSERT (sizeof (LEMONADES_RAMOOPS_HEADER) == 12, "Linux persistent_ram_buffer header is 12 bytes");
STATIC_ASSERT (OFFSET_OF (LEMONADES_RAMOOPS_HEADER, Start) == 4, "Linux start offset");
STATIC_ASSERT (OFFSET_OF (LEMONADES_RAMOOPS_HEADER, Size) == 8, "Linux size offset");
STATIC_ASSERT (LEMONADES_RAMOOPS_CONSOLE_BASE + LEMONADES_RAMOOPS_CONSOLE_SIZE == LEMONADES_RAMOOPS_PMSG_BASE,
               "Console must end before pmsg");
STATIC_ASSERT (LEMONADES_RAMOOPS_CAPACITY == 0x001FFFF4U, "ECC-free console payload size");

STATIC
volatile LEMONADES_RAMOOPS_HEADER *
ConsoleHeader (
  VOID
  )
{
  return (volatile LEMONADES_RAMOOPS_HEADER *)(UINTN)LEMONADES_RAMOOPS_CONSOLE_BASE;
}

STATIC
VOID
EnsureValidHeader (
  IN volatile LEMONADES_RAMOOPS_HEADER  *Header
  )
{
  UINT32  Start;
  UINT32  Size;

  Start = Header->Start;
  Size  = Header->Size;
  if ((Header->Signature == LEMONADES_RAMOOPS_SIGNATURE) &&
      (Size <= LEMONADES_RAMOOPS_CAPACITY) && (Start <= Size))
  {
    // Identical validity bounds to persistent_ram_post_init(). A full ring
    // with Start == Capacity is accepted by Linux and normalized on write.
    return;
  }

  Header->Signature = 0;
  MemoryFence ();
  Header->Start = 0;
  Header->Size  = 0;
  MemoryFence ();
  Header->Signature = LEMONADES_RAMOOPS_SIGNATURE;
  MemoryFence ();
}

RETURN_STATUS
EFIAPI
SerialPortInitialize (
  VOID
  )
{
  BOOLEAN  InterruptState;

  InterruptState = SaveAndDisableInterrupts ();
  EnsureValidHeader (ConsoleHeader ());
  SetInterruptState (InterruptState);
  return RETURN_SUCCESS;
}

UINTN
EFIAPI
SerialPortWrite (
  IN UINT8  *Buffer,
  IN UINTN  NumberOfBytes
  )
{
  volatile LEMONADES_RAMOOPS_HEADER  *Header;
  volatile UINT8              *Data;
  BOOLEAN                     InterruptState;
  UINTN                       Count;
  UINTN                       Index;
  UINT32                      Start;
  UINT32                      Size;

  if ((Buffer == NULL) || (NumberOfBytes == 0)) {
    return 0;
  }

  InterruptState = SaveAndDisableInterrupts ();
  Header         = ConsoleHeader ();
  EnsureValidHeader (Header);
  Data  = (volatile UINT8 *)(UINTN)(LEMONADES_RAMOOPS_CONSOLE_BASE + sizeof (*Header));
  Start = Header->Start;
  Size  = Header->Size;
  if (Start == LEMONADES_RAMOOPS_CAPACITY) {
    Start = 0;
  }

  // Like persistent_ram_write(), retain only the newest Capacity bytes of
  // an oversized individual write. Its returned count still acknowledges
  // the full input, matching the Linux persistent console write contract.
  Count = NumberOfBytes;
  if (Count > LEMONADES_RAMOOPS_CAPACITY) {
    Buffer += Count - LEMONADES_RAMOOPS_CAPACITY;
    Count   = LEMONADES_RAMOOPS_CAPACITY;
  }

  for (Index = 0; Index < Count; Index++) {
    Data[Start] = Buffer[Index];
    Start++;
    if (Start == LEMONADES_RAMOOPS_CAPACITY) {
      Start = 0;
    }
  }

  Size = (UINT32)(((UINTN)Size + Count > LEMONADES_RAMOOPS_CAPACITY) ?
                  LEMONADES_RAMOOPS_CAPACITY : (UINTN)Size + Count);
  // Publish payload before metadata. Size first keeps Start <= Size if a
  // reset interrupts the metadata update; a reset during a write can still
  // lose or partially expose its final message. This is not crash atomic.
  MemoryFence ();
  Header->Size = Size;
  MemoryFence ();
  Header->Start = Start;
  MemoryFence ();
  SetInterruptState (InterruptState);
  return NumberOfBytes;
}

UINTN
EFIAPI
SerialPortRead (
  OUT UINT8  *Buffer,
  IN UINTN   NumberOfBytes
  )
{
  return 0;
}

BOOLEAN
EFIAPI
SerialPortPoll (
  VOID
  )
{
  return FALSE;
}

RETURN_STATUS
EFIAPI
SerialPortSetControl (
  IN UINT32  Control
  )
{
  return RETURN_UNSUPPORTED;
}

RETURN_STATUS
EFIAPI
SerialPortGetControl (
  OUT UINT32  *Control
  )
{
  return RETURN_UNSUPPORTED;
}

RETURN_STATUS
EFIAPI
SerialPortSetAttributes (
  IN OUT UINT64              *BaudRate,
  IN OUT UINT32              *ReceiveFifoDepth,
  IN OUT UINT32              *Timeout,
  IN OUT EFI_PARITY_TYPE     *Parity,
  IN OUT UINT8               *DataBits,
  IN OUT EFI_STOP_BITS_TYPE  *StopBits
  )
{
  return RETURN_UNSUPPORTED;
}
