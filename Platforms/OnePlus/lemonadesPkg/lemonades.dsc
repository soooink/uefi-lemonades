## @file
# Xiaomi Mi 10 (lemonades), SM8250: temporary UEFI menu bring-up.
# SPDX-License-Identifier: BSD-2-Clause-Patent
##

[Defines]
  PLATFORM_NAME                  = lemonades
  PLATFORM_GUID                  = D02BA864-F1AD-4EDA-B76E-8C3504F173A1
  PLATFORM_VERSION               = 0.1
  DSC_SPECIFICATION              = 0x00010005
  OUTPUT_DIRECTORY               = Build/lemonadesPkg
  SUPPORTED_ARCHITECTURES        = AARCH64
  BUILD_TARGETS                  = DEBUG
  SKUID_IDENTIFIER               = DEFAULT
  FLASH_DEFINITION               = lemonadesPkg/lemonades.fdf
  USE_CUSTOM_DISPLAY_DRIVER      = 1
  SOC_TYPE                       = 0

!include KonaPkg/KonaPkg.dsc.inc

[BuildOptions]
  # The port must not draw through a guessed SEC framebuffer. SEC.c consumes
  # this opt-out; the unmodified stock DisplayDxe publishes the real GOP.
  *_CLANGPDB_AARCH64_CC_FLAGS = -D UMI_SKIP_EARLY_FRAMEBUFFER=1 -D UNIXNU_BRINGUP_MENU=1

[PcdsFixedAtBuild]
  gArmTokenSpaceGuid.PcdSystemMemoryBase|0x80000000
  gArmPlatformTokenSpaceGuid.PcdCPUCoresStackBase|0x9FF90000
  gArmPlatformTokenSpaceGuid.PcdCPUCorePrimaryStackSize|0x40000

  gSiliciumPkgTokenSpaceGuid.PcdSmbiosSystemManufacturer|"OnePlus"
  gSiliciumPkgTokenSpaceGuid.PcdSmbiosSystemModel|"OnePlus 9R"
  gSiliciumPkgTokenSpaceGuid.PcdSmbiosSystemRetailModel|"lemonades"
  gSiliciumPkgTokenSpaceGuid.PcdSmbiosSystemRetailSku|"lemonades-research"
  gSiliciumPkgTokenSpaceGuid.PcdSmbiosSystemBoardModel|"lemonades"
  gSiliciumPkgTokenSpaceGuid.PcdFrameBufferWidth|1080
  gSiliciumPkgTokenSpaceGuid.PcdFrameBufferHeight|2400
  gSiliciumPkgTokenSpaceGuid.PcdFrameBufferColorDepth|32
  gQcomPkgTokenSpaceGuid.PcdPlatformType|"LA"

  # UniXNUDxe writes standard boot variables. Keep their backend in RAM.
  gEfiMdeModulePkgTokenSpaceGuid.PcdEmuVariableNvModeEnable|TRUE
  gEfiMdeModulePkgTokenSpaceGuid.PcdResetOnMemoryTypeInformationChange|FALSE
  gEfiMdeModulePkgTokenSpaceGuid.PcdCapsuleInRamSupport|FALSE
  gUniXNUPkgTokenSpaceGuid.PcdUniXNUDebugLevel|0x80000000

[PcdsFeatureFlag]
  gEfiMdeModulePkgTokenSpaceGuid.PcdSupportUpdateCapsuleReset|FALSE
  gEfiMdeModulePkgTokenSpaceGuid.PcdSupportProcessCapsuleAtRuntime|FALSE

[LibraryClasses]
  MemoryMapLib|lemonadesPkg/Library/MemoryMapLib/MemoryMapLib.inf
  ConfigurationMapLib|lemonadesPkg/Library/ConfigurationMapLib/ConfigurationMapLib.inf
  SerialPortLib|lemonadesPkg/Library/FbSerialPortLib/FbSerialPortLib.inf
  DeviceBootManagerLib|SiliciumNullPkg/Library/PlatformBootManagerLibNull/PlatformBootManagerLibNull.inf
  PlatformBootOptionsLib|SiliciumNullPkg/Library/PlatformBootOptionsLibNull/PlatformBootOptionsLibNull.inf
  SecureBootHandlerLib|lemonadesPkg/Library/NullSecureBootHandlerLib/NullSecureBootHandlerLib.inf
  AcpiTableUpdateLib|SiliciumNullPkg/Library/AcpiTableUpdateLibNull/AcpiTableUpdateLibNull.inf
  RamManagerLib|SiliciumNullPkg/Library/RamManagerLibNull/RamManagerLibNull.inf
  UniXNULib|UniXNUPkg/Library/UniXNULib/UniXNULib.inf
  FdtLib|MdePkg/Library/BaseFdtLib/BaseFdtLib.inf
  CapsuleLib|MdeModulePkg/Library/DxeCapsuleLibNull/DxeCapsuleLibNull.inf

[Components]
  UniXNUPkg/Application/UniXNU/UniXNU.inf
  UniXNUPkg/Driver/UniXNUDxe/UniXNUDxe.inf

[Components]
  lemonadesPkg/Drivers/InheritedFbDxe/InheritedFbDxe.inf

[LibraryClasses]
  ResetSystemLib|lemonadesPkg/Library/LemonadesResetSystemLib/LemonadesResetSystemLib.inf
  HwResetSystemLib|lemonadesPkg/Library/LemonadesResetSystemLib/LemonadesResetSystemLib.inf
  LemonadesSpmiLib|lemonadesPkg/Library/LemonadesSpmiLib/LemonadesSpmiLib.inf
  LemonadesRpmhLib|lemonadesPkg/Library/LemonadesRpmhLib/LemonadesRpmhLib.inf

[Components]
  lemonadesPkg/Drivers/LemonadesButtonsDxe/LemonadesButtonsDxe.inf

[Components]
  MdeModulePkg/Universal/Disk/UnicodeCollation/EnglishDxe/EnglishDxe.inf
  MdeModulePkg/Universal/Disk/DiskIoDxe/DiskIoDxe.inf
  FatPkg/EnhancedFatDxe/Fat.inf
  lemonadesPkg/Drivers/LemonadesRamPayloadDxe/LemonadesRamPayloadDxe.inf
  lemonadesPkg/Application/LemonadesPayloadCheck/LemonadesPayloadCheck.inf
  lemonadesPkg/Application/LemonadesPayload64Check/LemonadesPayload64Check.inf
  lemonadesPkg/Application/LemonadesMemoryDiag/LemonadesMemoryDiag.inf

[Components]
  DarwinBootPkg/Driver/DarwinBoot.inf
