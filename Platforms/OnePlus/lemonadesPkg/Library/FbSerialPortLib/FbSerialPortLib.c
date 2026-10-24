/** @file
  lemonades on-screen serial console: every SerialPortWrite byte is drawn to
  the inherited splash framebuffer with an 8x16 font AND appended to the
  Linux ramoops console ring at 0xB0000000 (same contract as
  RamoopsSerialPortLib, so pstore keeps working wherever it can be read).

  No physical UART on this handset and OrangeFox does not parse the ramoops
  region, so the screen is the console. The ABL hands off with MMU and
  D-cache off and the panel self-refresh shows stores to 0x9C000000 at once
  (verified by the ios-topaz probe); after MMU setup the region is mapped
  write-through. The text cursor lives at a fixed address in the unused pmsg
  window so every module linked with this library continues the same screen.

  No allocation, Boot Services or DEBUG calls. Single core at bring-up.
  SPDX-License-Identifier: BSD-2-Clause-Patent
**/

#include <Base.h>
#include <Library/BaseLib.h>
#include <Library/SerialPortLib.h>
#include "font.h"

// Framebuffer geometry of the inherited splash (lemonades AMB655X).
#define LEMONADES_FB_BASE     0x9C000000ULL
#define LEMONADES_FB_WIDTH    1080U
#define LEMONADES_FB_HEIGHT   2400U
#define LEMONADES_FB_PIXELS   (LEMONADES_FB_WIDTH * LEMONADES_FB_HEIGHT)

// Text console below the stage-color band.
#define LEMONADES_TEXT_TOP    80U
#define LEMONADES_COLS        (LEMONADES_FB_WIDTH / 8U)
#define LEMONADES_ROWS        ((LEMONADES_FB_HEIGHT - LEMONADES_TEXT_TOP) / 16U)
#define LEMONADES_FG          0x00FFFFFFU   /* BGRA white */
#define LEMONADES_BG          0x00000000U   /* BGRA black */

// The cursor (col, row) sits in the pmsg window, which nothing else uses.
#define LEMONADES_CURSOR_BASE 0xB0201000ULL

// ramoops console, identical to RamoopsSerialPortLib.
#define LEMONADES_RAMOOPS_CONSOLE_BASE  0xB0000000ULL
#define LEMONADES_RAMOOPS_CONSOLE_SIZE  0x00200000U
#define LEMONADES_RAMOOPS_SIGNATURE     0x43474244U

typedef struct {
  UINT32  Signature;
  UINT32  Start;
  UINT32  Size;
} LEMONADES_RAMOOPS_HEADER;

#define LEMONADES_RAMOOPS_CAPACITY  (LEMONADES_RAMOOPS_CONSOLE_SIZE - sizeof (LEMONADES_RAMOOPS_HEADER))

STATIC_ASSERT (sizeof (LEMONADES_RAMOOPS_HEADER) == 12, "Linux persistent_ram_buffer header is 12 bytes");
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
volatile UINT32 *
CursorCell (
  VOID
  )
{
  return (volatile UINT32 *)(UINTN)LEMONADES_CURSOR_BASE;
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

STATIC
volatile UINT32 *
Pixel (
  IN UINTN  X,
  IN UINTN  Y
  )
{
  return (volatile UINT32 *)(UINTN)(LEMONADES_FB_BASE + (Y * LEMONADES_FB_WIDTH + X) * 4U);
}

STATIC
VOID
DrawGlyph (
  IN UINTN  Col,
  IN UINTN  Row,
  IN UINT8  Char
  )
{
  CONST UINT8  *Glyph;
  UINTN         BaseX;
  UINTN         BaseY;
  UINTN         Line;
  UINTN         Bit;

  if ((Char < 0x20) || (Char > 0x7e)) {
    Char = '?';
  }
  Glyph  = font8x16[Char - 0x20];
  BaseX  = Col * 8U;
  BaseY  = LEMONADES_TEXT_TOP + Row * 16U;
  for (Line = 0; Line < 16U; ++Line) {
    UINT8  Bits = Glyph[Line];
    for (Bit = 0; Bit < 8U; ++Bit) {
      *Pixel (BaseX + Bit, BaseY + Line) = ((Bits >> (7U - Bit)) & 1U) ? LEMONADES_FG : LEMONADES_BG;
    }
  }
}

STATIC
VOID
ScrollUp (
  VOID
  )
{
  UINTN  X;
  UINTN  Y;
  UINTN  Line;

  for (Y = 0; Y < LEMONADES_ROWS - 1U; ++Y) {
    for (Line = 0; Line < 16U; ++Line) {
      for (X = 0; X < LEMONADES_FB_WIDTH; ++X) {
        *Pixel (X, LEMONADES_TEXT_TOP + Y * 16U + Line) =
          *Pixel (X, LEMONADES_TEXT_TOP + (Y + 1U) * 16U + Line);
      }
    }
  }
  for (X = 0; X < LEMONADES_FB_WIDTH; ++X) {
    UINTN  Yv;
    for (Yv = (LEMONADES_ROWS - 1U) * 16U; Yv < LEMONADES_ROWS * 16U; ++Yv) {
      *Pixel (X, LEMONADES_TEXT_TOP + Yv) = LEMONADES_BG;
    }
  }
}

STATIC
VOID
Newline (
  VOID
  )
{
  volatile UINT32  *Cursor = CursorCell ();

  Cursor[0] = 0;
  Cursor[1]++;
  if (Cursor[1] >= LEMONADES_ROWS) {
    Cursor[1] = LEMONADES_ROWS - 1U;
    ScrollUp ();
  }
}

STATIC
VOID
PutChar (
  IN UINT8  Char
  )
{
  volatile UINT32  *Cursor = CursorCell ();
  UINTN             Col    = Cursor[0];
  UINTN             Row    = Cursor[1];

  if (Col >= LEMONADES_COLS) {
    Col = 0;
    Newline ();
    Row = Cursor[1];
  }
  if (Row >= LEMONADES_ROWS) {
    Row = Cursor[1] = LEMONADES_ROWS - 1U;
  }

  if (Char == '\r') {
    return;
  }
  if (Char == '\n') {
    Newline ();
    return;
  }
  if ((Char < 0x20) || (Char > 0x7e)) {
    Char = '.';
  }

  DrawGlyph (Col, Row, Char);
  Col++;
  if (Col >= LEMONADES_COLS) {
    Newline ();
    return;
  }
  Cursor[0] = (UINT32)Col;
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
  // The cursor stays wherever the previous module left it; a fresh boot
  // starts from garbage, so clamp it into range instead of resetting.
  if ((CursorCell ()[0] >= LEMONADES_COLS) || (CursorCell ()[1] >= LEMONADES_ROWS)) {
    CursorCell ()[0] = 0;
    CursorCell ()[1] = 0;
  }
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
  volatile UINT8                     *Data;
  BOOLEAN                            InterruptState;
  UINTN                              Count;
  UINTN                              Index;
  UINT32                             Start;
  UINT32                             Size;

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
    PutChar (Buffer[Index]);
  }

  Size = (UINT32)(((UINTN)Size + Count > LEMONADES_RAMOOPS_CAPACITY) ?
                  LEMONADES_RAMOOPS_CAPACITY : (UINTN)Size + Count);
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
