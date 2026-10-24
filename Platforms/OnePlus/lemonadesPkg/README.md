# lemonades UEFI bring-up (OnePlus 9R)

Derived from the Xiaomi Mi 10 (`umiPkg`) UniXNU bring-up at reAAAq attempt 80,
which is the proven SM8250/kona baseline: Mu-Silicium on Kona, inherited
framebuffer GOP, PM8150 PON power key, ramoops console, RAM FAT payload and
DarwinBoot.

## Device deltas versus umi

| Item | umi (Mi 10) | lemonades (OnePlus 9R) |
| --- | --- | --- |
| SoC | SM8250 | SM8250-AC (Snapdragon 870), same kona MMIO |
| Panel | 1080x2340 (J2 MP) | Samsung AMB655X 1080x2400, DSC command mode |
| Splash / inherited GOP | 0x9c000000, pitch 4320 | 0x9c000000, pitch 4320 (same) |
| UEFI FD region | 0x9fc00000 + 0x300000 | same (xbl uefiplat.cfg) |
| TZApps Reserved | 0x82400000 + 0x3a00000 | 0x82400000 + 0x0B400000 |
| PIL Reserved | 0x86000000 + 0xd200000 | 0x8DA00000 + 0x0AD00000 |
| Volume keys | PM8150 PON bit1 / PMIC GPIO6 | TLMM GPIO 7 / GPIO 6 (SOUTH tile, active low) |
| Boot image | header v1, gzip, `fastboot boot` works | **header v2**, ABL rejects `fastboot boot`; flash `boot_b` |
| Touch | ST FTM5 (I2C QUP) | Synaptics S3908 (GENI 0xa94000, addr 0x4B) — not in UEFI |
| ramoops console | 0xB0000000 | 0xB0000000 (same) |

`Resources/Configs/lemonades.toml` defaults `kernel_compression = "none"`
because the stock boot_b kernel is an uncompressed Image+DTB and this ABL's
gzip support is unverified. The device DTB in `Resources/DTBs/lemonades.dtb`
is the stock boot_b appended DTB (OPLUS project 20828, kona MTP).

## Status

Scaffold. Nothing here has run on hardware. Bring-up order and the flash /
restore procedure are documented in the UniXNU-lemonades repository.
