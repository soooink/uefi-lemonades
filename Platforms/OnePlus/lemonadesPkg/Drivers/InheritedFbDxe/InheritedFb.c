/** @file
  CPU-only GOP experiment. No display, clock, PMIC or SMMU programming.

  Attempts 06-08 observed this layout after vendor GOP initialization. The
  reserved range also matches the handset DT. Neither observation establishes
  that ABL is still scanning out this layout, or refreshing the command panel.
  Physical visibility must be tested independently of successful memory Blts.

  SPDX-License-Identifier: BSD-2-Clause-Patent
**/
#include <Uefi.h>
#include <Protocol/GraphicsOutput.h>
#include <Library/BaseMemoryLib.h>
#include <Library/DebugLib.h>
#include <Library/FrameBufferBltLib.h>
#include <Library/MemoryAllocationLib.h>
#include <Library/MemoryMapHelperLib.h>
#include <Library/UefiBootServicesTableLib.h>
#include <Configuration/BootDevices.h>

#define LEMONADES_FB_BASE   0x9C000000ULL
#define LEMONADES_FB_BYTES  (1080U * 2400U * sizeof (EFI_GRAPHICS_OUTPUT_BLT_PIXEL))

STATIC FRAME_BUFFER_CONFIGURE *mBltConfiguration;
STATIC EFI_GRAPHICS_OUTPUT_MODE_INFORMATION mInfo = {
  0, 1080, 2400, PixelBlueGreenRedReserved8BitPerColor, { 0, 0, 0, 0 }, 1080
};
STATIC EFI_GRAPHICS_OUTPUT_PROTOCOL_MODE mMode = {
  1, 0, &mInfo, sizeof (mInfo), LEMONADES_FB_BASE, LEMONADES_FB_BYTES
};

STATIC
EFI_STATUS
EFIAPI
LemonadesQueryMode (
  IN EFI_GRAPHICS_OUTPUT_PROTOCOL           *This,
  IN UINT32                                 ModeNumber,
  OUT UINTN                                *SizeOfInfo,
  OUT EFI_GRAPHICS_OUTPUT_MODE_INFORMATION **Info)
{
  if ((This == NULL) || (SizeOfInfo == NULL) || (Info == NULL)) {
    return EFI_INVALID_PARAMETER;
  }
  if (ModeNumber != 0) {
    return EFI_INVALID_PARAMETER;
  }
  *Info = AllocateCopyPool (sizeof (mInfo), &mInfo);
  if (*Info == NULL) {
    return EFI_OUT_OF_RESOURCES;
  }
  *SizeOfInfo = sizeof (mInfo);
  return EFI_SUCCESS;
}

STATIC
EFI_STATUS
EFIAPI
LemonadesBlt (
  IN EFI_GRAPHICS_OUTPUT_PROTOCOL      *This,
  IN EFI_GRAPHICS_OUTPUT_BLT_PIXEL     *Buffer OPTIONAL,
  IN EFI_GRAPHICS_OUTPUT_BLT_OPERATION  Operation,
  IN UINTN SourceX, IN UINTN SourceY,
  IN UINTN DestinationX, IN UINTN DestinationY,
  IN UINTN Width, IN UINTN Height, IN UINTN Delta OPTIONAL)
{
  EFI_STATUS Status;
  EFI_TPL OldTpl;

  if ((This == NULL) || (mBltConfiguration == NULL) ||
      (Operation >= EfiGraphicsOutputBltOperationMax) ||
      ((Buffer == NULL) && (Operation != EfiBltVideoToVideo))) {
    return EFI_INVALID_PARAMETER;
  }
  OldTpl = gBS->RaiseTPL (TPL_NOTIFY);
  Status = FrameBufferBlt (mBltConfiguration, Buffer, Operation,
                          SourceX, SourceY, DestinationX, DestinationY,
                          Width, Height, Delta);
  gBS->RestoreTPL (OldTpl);
  return Status;
}

STATIC
EFI_STATUS
EFIAPI
LemonadesSetMode (
  IN EFI_GRAPHICS_OUTPUT_PROTOCOL *This,
  IN UINT32 ModeNumber)
{
  EFI_GRAPHICS_OUTPUT_BLT_PIXEL Black = { 0, 0, 0, 0 };

  if (This == NULL) {
    return EFI_INVALID_PARAMETER;
  }
  if (ModeNumber != 0) {
    return EFI_UNSUPPORTED;
  }
  // GOP requires clearing on SetMode. This only writes reserved framebuffer
  // RAM; it does not reconfigure the panel or trigger a DSI refresh.
  return LemonadesBlt (This, &Black, EfiBltVideoFill, 0, 0, 0, 0,
                 mInfo.HorizontalResolution, mInfo.VerticalResolution, 0);
}

STATIC EFI_GRAPHICS_OUTPUT_PROTOCOL mGop = {
  LemonadesQueryMode, LemonadesSetMode, LemonadesBlt, &mMode
};

EFI_STATUS
EFIAPI
LemonadesInheritedFbEntry (
  IN EFI_HANDLE ImageHandle,
  IN EFI_SYSTEM_TABLE *SystemTable)
{
  EFI_MEMORY_REGION_DESCRIPTOR Region;
  EFI_STATUS Status;
  EFI_HANDLE Handle = NULL;
  UINTN ConfigurationSize = 0;

  {
    /* blue: DXE reached the inherited display driver */
    /* top band only: keep the on-screen console text below it */
    volatile UINT32 *Pixel = (volatile UINT32 *)(UINTN)LEMONADES_FB_BASE;
    UINTN Count;
    for (Count = 0; Count < (64u * 1080u); ++Count) {
      Pixel[Count] = 0x000000FFu;
    }
  }
  ZeroMem (&Region, sizeof (Region));
  Status = LocateMemoryRegionByName ("Display Reserved", &Region);
  if (EFI_ERROR (Status)) {
    return Status;
  }
  if ((Region.Address != LEMONADES_FB_BASE) || (Region.Length < LEMONADES_FB_BYTES)) {
    DEBUG ((EFI_D_ERROR, "UMI: inherited framebuffer reservation mismatch\n"));
    return EFI_UNSUPPORTED;
  }
  Status = FrameBufferBltConfigure ((VOID *)(UINTN)LEMONADES_FB_BASE, &mInfo,
                                    NULL, &ConfigurationSize);
  if ((Status != EFI_BUFFER_TOO_SMALL) || (ConfigurationSize == 0)) {
    return EFI_DEVICE_ERROR;
  }
  mBltConfiguration = AllocatePool (ConfigurationSize);
  if (mBltConfiguration == NULL) {
    return EFI_OUT_OF_RESOURCES;
  }
  Status = FrameBufferBltConfigure ((VOID *)(UINTN)LEMONADES_FB_BASE, &mInfo,
                                    mBltConfiguration, &ConfigurationSize);
  if (!EFI_ERROR (Status)) {
    Status = gBS->InstallMultipleProtocolInterfaces (
                    &Handle,
                    &gEfiDevicePathProtocolGuid, &DisplayDevicePath,
                    &gEfiGraphicsOutputProtocolGuid, &mGop,
                    NULL);
  }
  if (EFI_ERROR (Status)) {
    FreePool (mBltConfiguration);
    mBltConfiguration = NULL;
    return Status;
  }
  DEBUG ((EFI_D_ERROR,
          "UMI: inherited GOP installed; candidate layout 9c000000 1080x2400 BGRX32; scanout unverified\n"));
  return EFI_SUCCESS;
}
