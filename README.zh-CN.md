# uefi-umi — 小米 10

[English](README.md)

小米 10（umi / SM8250）的 Mu-Silicium UEFI 移植，基于
[uefi-sheng](https://github.com/UniXNU/uefi-sheng)，配合
[UniXNU-umi](https://github.com/reAAAq/UniXNU-umi) 使用。
已在 12 GiB / 256 GB 手机上进入 iPadOS 17.7.11 Hello／设置向导。
第 80 版已由机主确认 XNU 下 ST FTS 触摸可用，修复代码位于配套设备仓库，
见[验证摘要](https://github.com/reAAAq/UniXNU-umi/blob/main/docs/validation.json)。

已验证继承显存 GOP、菜单、音量选择、电源短按确认、暖重启、只读 RAM FAT
负载和 DarwinBoot 交接。`Platforms/Xiaomi/umiPkg` 为板级代码，
`Resources/Configs/umi.toml` 为镜像配置；当前保存的是已组装的继承显示、实体键和
RAM 负载配置。生成模板和构建工具位于设备仓库。UEFI ST FTS 代码仅供继续调试。

早期 shim／SEC 日志使用 umi 专属地址；本分支仅验证 umi，不声明其他继承机型可用。
构建固定版本、外部输入和限制见设备仓库的
[BUILD.md](https://github.com/reAAAq/UniXNU-umi/blob/main/docs/BUILD.md)。
尚不是通用脚本一键安装流程。仓库不包含本机 boot 镜像、厂商固件、设备转储或 Apple 组件，
不要套用其他设备的 DTB、内存和分区布局；实验前保留 Recovery 与备份。

原项目说明、致谢和许可摘要保留在 [README.upstream.md](README.upstream.md)，
各目录和文件原许可继续有效。由 [reAAAq](https://github.com/reAAAq) 独立维护。
