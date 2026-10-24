# uefi-umi — Xiaomi Mi 10

[简体中文](README.zh-CN.md)

Mu-Silicium UEFI port for **Xiaomi Mi 10 (`umi`, SM8250)**, derived from
[uefi-sheng](https://github.com/UniXNU/uefi-sheng). Paired with
[UniXNU-umi](https://github.com/reAAAq/UniXNU-umi), the tested 12 GiB / 256 GB phone
reaches iPadOS 17.7.11 Hello / Setup Assistant. Physical ST FTS touch under XNU
was confirmed working by the device owner in attempt 80; the fix is in the paired
device repository. See its [validation summary](https://github.com/reAAAq/UniXNU-umi/blob/main/docs/validation.json).

## Verified firmware path

- Inherited framebuffer GOP, UniXNU menu and CPU text/pixel output.
- Volume keys for selection, short power-key confirmation and warm restart.
- Read-only RAM FAT payload, external EFI checks and DarwinBoot handoff.
- Measured memory map, early RAM-console tracing and SMMU observations.

`Platforms/Xiaomi/umiPkg` contains the board port;
`Resources/Configs/umi.toml` describes the boot image. The checked-in platform is
the staged inherited-display/buttons/RAM-payload profile. The device repository
contains the input overlays and staging scripts. The experimental UEFI ST FTS
code is retained for investigation and is not a working touch implementation.

The early shim and SEC diagnostics use umi-specific addresses. This branch is
for umi only; inherited configurations for other devices are not qualified here.

## Build and inputs

Use the source pins and [build notes](https://github.com/reAAAq/UniXNU-umi/blob/main/docs/BUILD.md)
in the paired device repository. Its host builder stages the UniXNU package and
DarwinBoot, applies Mu patches, imports locally verified firmware inputs and
packs the board's measured DTB. Generic upstream build instructions alone do not
reproduce this experimental profile. Do not substitute another board's DTB,
memory map or partition layout.

No umi boot image, vendor firmware, device dump or Apple restore component is
included. Existing upstream submodule links and resources retain their provenance.
No general installer is provided, and there is no XNU USB-support claim based on
Recovery ADB. Preserve a recovery route before experimenting.

## Credits and license

The original project description, credits and license summary are preserved in
[README.upstream.md](README.upstream.md). Existing directory and file licenses
apply; new umi files carry their own SPDX notices. This is an independent port
maintained by [reAAAq](https://github.com/reAAAq), not an official UniXNU device.
