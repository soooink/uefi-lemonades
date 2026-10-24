/** @file
  OnePlus 9R (lemonades) buttons: all three keys are PM8150 SID 0 SPMI
  peripherals, read through v5 arbiter observer commands.

  Board DT (lemonades, OPLUS project 20828): gpio_keys references the SPMI
  PMIC pinctrl (phandle 0x625 inside qcom,spmi-pmic), with key states
  key_vol_up/vol_down on pmic gpio 6/7 - NOT TLMM GPIOs; reading TLMM 6/7
  shows stuck-low pads. Power key: PON 0x800 like the umi port. Linux
  reports arbiter 0x50010000.
  Register layout and command format: LineageOS SM8250 71b13e62,
  drivers/spmi/spmi-pmic-arb.c, drivers/input/misc/qpnp-power-on.c and
  drivers/pinctrl/qcom/pinctrl-spmi-gpio.c. Each PMIC GPIO is its own SPMI
  peripheral: gpio n = PPID 0x0C0 + (n - 1), the STATUS register at offset
  0x10 with the level in bit 0 (active low). Volume up = gpio 6 (0x0C5),
  volume down = gpio 7 (0x0C6), exactly the peripheral the umi port proved.

  Only observer READs are issued. No PMIC write, interrupt acknowledgement,
  reset setting, ownership or GPIO configuration changes.
  SPDX-License-Identifier: BSD-2-Clause-Patent
**/
#include <Uefi.h>
#include <Guid/EventGroup.h>
#include <Protocol/SimpleTextIn.h>
#include <Protocol/GraphicsOutput.h>
#include <Library/DebugLib.h>
#include <Library/IoLib.h>
#include <Library/TimerLib.h>
#include <Library/UefiBootServicesTableLib.h>
#include <Configuration/BootDevices.h>
#include "KeyFilter.h"

#define ARB_CORE       0x0C440000U
#define ARB_CONFIG     0x0C40A000U
#define ARB_OBSERVER   0x0E600000U
#define ARB_CHANNELS   512U
#define QUEUE_SIZE     16U

// PM8150 (SID 0) GPIO peripherals: gpio 6 -> PPID 0x0C5, gpio 7 -> 0x0C6.
#define PPID_PON       0x008U
#define PPID_GPIO6     0x0C5U
#define PPID_GPIO7     0x0C6U

STATIC UINTN mPonChannel;
STATIC UINTN mGpio6Channel;
STATIC UINTN mGpio7Channel;
STATIC EFI_EVENT mTimer;
STATIC EFI_EVENT mExitEvent;
STATIC EFI_INPUT_KEY mQueue[QUEUE_SIZE];
STATIC UINTN mHead;
STATIC UINTN mTail;
STATIC UINTN mReadErrors;
STATIC UINTN mLoggedKeys;
STATIC UINTN mLoggedResyncs;
STATIC BOOLEAN mResync = TRUE;
STATIC BOOLEAN mStopped;
STATIC LEMONADES_KEY_FILTER mFilter;
STATIC EFI_SIMPLE_TEXT_INPUT_PROTOCOL mInput;

// lemonades bring-up: GraphicsConsole clears the framebuffer when the menu
// comes up, wiping anything painted at dispatch time. The poll loop therefore
// repaints a status strip at y=64..80 every tick: green = SimpleTextIn
// installed, red = entry failed, black = driver not dispatched. Three 32x16
// squares show the RAW key mask live (vol-up, vol-down, power): a square
// lights while the key reads as pressed, whatever happens to the menu.
STATIC BOOLEAN  mInstalled;

STATIC
VOID
PaintStatus (
  IN UINT8      Mask,
  IN EFI_STATUS ReadStatus
  )
{
  volatile UINT32  *Base = (volatile UINT32 *)(UINTN)0x9C000000ULL;
  UINTN             i, Square, X, Y;
  UINT32            BaseColor;

  BaseColor = mInstalled ? 0x0000FF00u :        /* green: installed   */
              EFI_ERROR (ReadStatus) ? 0x00FF0000u : 0x00404040u;
  for (i = 0; i < 16u * 1080u; ++i) {
    Base[64u * 1080u + i] = BaseColor;
  }
  /* raw key squares: x=320 (vol-up), x=420 (vol-down), x=520 (power) */
  for (Square = 0; Square < 3u; ++Square) {
    UINT32  Color = ((Mask & (1u << Square)) != 0) ? 0x00FFFFFFu : 0x00000000u;
    UINTN   X0    = 320u + Square * 100u;
    for (Y = 66u; Y < 78u; ++Y) {
      for (X = X0; X < X0 + 32u; ++X) {
        Base[Y * 1080u + X] = Color;
      }
    }
  }
}

STATIC EFI_STATUS
FindObserverChannel (UINT16 Ppid, UINTN *Channel)
{
  UINT32 Mapping;
  UINT32 Owner;
  UINTN Index;
  BOOLEAN Found = FALSE;

  for (Index = 0; Index < ARB_CHANNELS; ++Index) {
    Mapping = MmioRead32 (ARB_CORE + 0x900 + Index * 4);
    if ((Mapping == 0) || (((Mapping >> 8) & 0xFFF) != Ppid)) {
      continue;
    }
    Owner = MmioRead32 (ARB_CONFIG + 0x700 + Index * 4) & 7;
    // Match Linux's v5 selection: first mapping, overridden by an EE0 owner.
    if (!Found || (Owner == 0)) {
      *Channel = ARB_OBSERVER + 0x80 * Index;
      Found = TRUE;
    }
    DEBUG ((EFI_D_ERROR, "LEMONADES keys: PPID=%03x APID=%u owner=%u\n",
            Ppid, (UINT32)Index, Owner));
  }
  return Found ? EFI_SUCCESS : EFI_NOT_FOUND;
}

STATIC EFI_STATUS
ReadStatusByte (UINTN Channel, UINT8 *Value)
{
  UINTN Try;
  UINT32 Status;

  // EXT_READL = 1, peripheral offset 0x10, length 1 (byte count field 0).
  // SID/peripheral are selected by APID, not by the command's address field.
  MmioWrite32 (Channel, (1U << 27) | (0x10U << 4));
  for (Try = 0; Try < 1000; ++Try) {
    Status = MmioRead32 (Channel + 0x08);
    if ((Status & BIT0) != 0) {
      if ((Status & (BIT1 | BIT2 | BIT3)) != 0) {
        return EFI_DEVICE_ERROR;
      }
      *Value = (UINT8)MmioRead32 (Channel + 0x18);
      return EFI_SUCCESS;
    }
    MicroSecondDelay (1);
  }
  return EFI_TIMEOUT;
}

STATIC EFI_STATUS
ReadKeys (UINT8 *Mask)
{
  EFI_STATUS Status;
  UINT8 Pon;
  UINT8 Gpio6;
  UINT8 Gpio7;

  Status = ReadStatusByte (mPonChannel, &Pon);
  if (EFI_ERROR (Status)) {
    return Status;
  }
  Status = ReadStatusByte (mGpio6Channel, &Gpio6);
  if (EFI_ERROR (Status)) {
    return Status;
  }
  Status = ReadStatusByte (mGpio7Channel, &Gpio7);
  if (EFI_ERROR (Status)) {
    return Status;
  }
  // Active-low everywhere: PON and GPIO status bits read 0 when pressed.
  *Mask = ((Gpio6 & BIT0) ? 0 : BIT0) |
          ((Gpio7 & BIT0) ? 0 : BIT1) |
          ((Pon & BIT0) ? BIT2 : 0);
  return EFI_SUCCESS;
}

STATIC VOID
EFIAPI
PollKeys (EFI_EVENT Event, VOID *Context)
{
  EFI_STATUS Status;
  UINT8 Mask;
  UINT8 Press;
  UINT64 NowUs;
  UINTN Next;

  if (mStopped) {
    return;
  }
  Status = ReadKeys (&Mask);
  PaintStatus (Mask, Status);
  if (EFI_ERROR (Status)) {
    if (mReadErrors++ < 4) {
      DEBUG ((EFI_D_ERROR, "LEMONADES keys: status read failed %r\n", Status));
    }
    mResync = TRUE;
    return;
  }
  NowUs = GetTimeInNanoSecond (GetPerformanceCounter ()) / 1000;
  if (mResync) {
    LemonadesKeyFilterInit (&mFilter, Mask, NowUs);
    mResync = FALSE;
    if (mLoggedResyncs++ < 8) {
      DEBUG ((EFI_D_ERROR, "LEMONADES keys: initial/resync mask=%02x\n", Mask));
    }
    return;
  }
  Press = LemonadesKeyFilterUpdate (&mFilter, Mask, NowUs);
  if (Press == 0) {
    return;
  }
  Next = (mTail + 1) % QUEUE_SIZE;
  if (Next == mHead) {
    return;
  }
  mQueue[mTail].ScanCode = (Press == BIT0) ? SCAN_UP :
                          (Press == BIT1) ? SCAN_DOWN : SCAN_NULL;
  mQueue[mTail].UnicodeChar = (Press == BIT2) ? CHAR_CARRIAGE_RETURN : 0;
  mTail = Next;
  if (mLoggedKeys++ < 64) {
    DEBUG ((EFI_D_ERROR, "LEMONADES keys: event=%a raw=%02x\n",
            (Press == BIT0) ? "volume-up" : (Press == BIT1) ? "volume-down" : "power",
            Mask));
  }
  gBS->SignalEvent (mInput.WaitForKey);
}

STATIC EFI_STATUS
EFIAPI
ResetKeys (EFI_SIMPLE_TEXT_INPUT_PROTOCOL *This, BOOLEAN ExtendedVerification)
{
  EFI_TPL OldTpl;
  if (This != &mInput) {
    return EFI_INVALID_PARAMETER;
  }
  OldTpl = gBS->RaiseTPL (TPL_NOTIFY);
  mHead = mTail = 0;
  mResync = TRUE;
  gBS->RestoreTPL (OldTpl);
  return EFI_SUCCESS;
}

STATIC EFI_STATUS
EFIAPI
ReadKey (EFI_SIMPLE_TEXT_INPUT_PROTOCOL *This, EFI_INPUT_KEY *Key)
{
  EFI_TPL OldTpl;
  EFI_STATUS Status = EFI_NOT_READY;
  if ((This != &mInput) || (Key == NULL)) {
    return EFI_INVALID_PARAMETER;
  }
  OldTpl = gBS->RaiseTPL (TPL_NOTIFY);
  if (mHead != mTail) {
    *Key = mQueue[mHead];
    mHead = (mHead + 1) % QUEUE_SIZE;
    Status = EFI_SUCCESS;
  }
  gBS->RestoreTPL (OldTpl);
  return Status;
}

STATIC VOID
EFIAPI
WaitForKey (EFI_EVENT Event, VOID *Context)
{
  if (mHead != mTail) {
    gBS->SignalEvent (Event);
  }
}

STATIC VOID
EFIAPI
StopKeys (EFI_EVENT Event, VOID *Context)
{
  mStopped = TRUE;
  gBS->SetTimer (mTimer, TimerCancel, 0);
}

EFI_STATUS
EFIAPI
LemonadesButtonsEntry (EFI_HANDLE ImageHandle, EFI_SYSTEM_TABLE *SystemTable)
{
  EFI_STATUS Status;
  EFI_HANDLE Handle = NULL;
  UINT32 Version;
  UINT8 Mask;

  Version = MmioRead32 (ARB_CORE);
  DEBUG ((EFI_D_ERROR, "LEMONADES keys: arbiter=%08x EE=0\n", Version));
  if (Version != 0x50010000) {
    DEBUG ((EFI_D_ERROR, "LEMONADES keys: arbiter version mismatch\n"));
    return EFI_UNSUPPORTED;
  }
  Status = FindObserverChannel (PPID_PON, &mPonChannel);
  if (EFI_ERROR (Status)) {
    DEBUG ((EFI_D_ERROR, "LEMONADES keys: PON observer channel not found\n"));
    return Status;
  }
  Status = FindObserverChannel (PPID_GPIO6, &mGpio6Channel);
  if (EFI_ERROR (Status)) {
    DEBUG ((EFI_D_ERROR, "LEMONADES keys: gpio6 observer channel not found\n"));
    return Status;
  }
  Status = FindObserverChannel (PPID_GPIO7, &mGpio7Channel);
  if (EFI_ERROR (Status)) {
    DEBUG ((EFI_D_ERROR, "LEMONADES keys: gpio7 observer channel not found\n"));
    return Status;
  }
  Status = ReadKeys (&Mask);
  DEBUG ((EFI_D_ERROR, "LEMONADES keys: initial read %r\n", Status));
  if (EFI_ERROR (Status)) {
    return Status;
  }
  mInput.Reset = ResetKeys;
  mInput.ReadKeyStroke = ReadKey;
  Status = gBS->CreateEvent (EVT_NOTIFY_WAIT, TPL_NOTIFY, WaitForKey, NULL, &mInput.WaitForKey);
  if (EFI_ERROR (Status)) {
    return Status;
  }
  Status = gBS->CreateEvent (EVT_TIMER | EVT_NOTIFY_SIGNAL, TPL_NOTIFY, PollKeys, NULL, &mTimer);
  if (EFI_ERROR (Status)) {
    goto Cleanup;
  }
  Status = gBS->CreateEventEx (EVT_NOTIFY_SIGNAL, TPL_NOTIFY, StopKeys, NULL,
                              &gEfiEventExitBootServicesGuid, &mExitEvent);
  if (EFI_ERROR (Status)) {
    goto Cleanup;
  }
  Status = gBS->SetTimer (mTimer, TimerPeriodic, 100000); // 10 ms.
  if (EFI_ERROR (Status)) {
    goto Cleanup;
  }
  // Match the ConIn path already registered by PlatformBootManagerBeforeConsole.
  Status = gBS->InstallMultipleProtocolInterfaces (
                  &Handle, &gEfiDevicePathProtocolGuid, &KeypadDevicePath,
                  &gEfiSimpleTextInProtocolGuid, &mInput, NULL);
  if (!EFI_ERROR (Status)) {
    DEBUG ((EFI_D_ERROR, "LEMONADES keys: SimpleTextIn installed; volume select, short power release confirms\n"));
    mInstalled = TRUE;
    return EFI_SUCCESS;
  }
Cleanup:
  if (mTimer != NULL) {
    gBS->CloseEvent (mTimer);
  }
  if (mExitEvent != NULL) {
    gBS->CloseEvent (mExitEvent);
  }
  gBS->CloseEvent (mInput.WaitForKey);
  return Status;
}
