# 临时Android取证安全预检（2026-11-07）

用户选择：先核对已有Android启动条件，**只有安全可行时**临时启动取证，
不刷任何分区、不写校准区、不发送试验SMC。正常Android驱动加载的硬件
状态变化已说明；这个授权不允许格式化或破坏已安装的iOS根文件系统。

## 预检结果：拒绝启动现有三份boot

项目安装记录确认userdata已换成16GiB iOS HFSX；这不是Android的f2fs。
从本地原件boot头定位ramdisk，在主机RAM解压并解析newc cpio，核对实际
携带fstab，而不只凭本地device源树猜测。三份镜像都包含：

- `/data` → userdata，f2fs，`formattable`及加密/检查相关参数。
- `/metadata` → metadata，ext4，`formattable,check,first_stage_mount`。
- `/mnt/vendor/persist` → persist，ext4，无`ro`，并有`check`。

**fastboot boot不写boot分区不意味着随后Android只读**。init/fs_mgr/vold
可能写入/格式化data或metadata，persist挂载/检查也不符合校准区保护范围。
静态标志只证明风险，本轮没有真的启动或观察格式化；不能说数据已损坏。
不通过删除一个formattable标志冒称完整Android只读。

| 原件 | SHA256 | header | fstab成员 |
|---|---|---:|---|
| `../backup/boot.img` | `d9e8ca63f295042b8f1dcb68e9bb2d18200e142970187378f296264551f579c0` | 2 | system/etc/fstab.qcom |
| `/home/yangqi/android/out/target/product/cepheus/boot.img` | `b96e0bac7c7edc8f6a48cc15b5bd66634ca3044ecc195769ed9526690f9483a9` | 2 | fstab.qcom |
| `/home/yangqi/edlflash-backups/backup-1792165318400822136/boot.img` | `48bb58a204a72e6d1d59623526a99cbad412fe2a7f5e3b59bce2b57e8b26678c` | 0 | fstab.qcom |

完整fstab身份/结果见`../logs/android-pas13-preflight-20261107/result.json`。
原件未改写，未解压到分区或执行镜像程序。此阶段没有改动XNU/启动参数。

## 手机状态及实际操作

运行主机`fastboot devices`、`adb devices -l`、`lsusb`时未见手机；预期
serial67eea163未能确认，当前手机运行状态未知，不能推断自动重启/故障。
只做主机枚举，没有ADB shell/分区读取、fastboot boot、reboot或flash。
未触碰persist/modemst1/2/fsg/fsc，未发新SMC、认证、复位或电源试写。

## 后续安全路径（尚未实现/验收）

若无匹配本机的TZ原件可直接提供，应另外准备**RAM-only最小取证initramfs**，
避免启动完整Android或未经核验的TWRP。候选至少必须满足：

1. 不运行Android init/vold/fs_mgr或recovery自动动作；无需安装userdata。
2. 不挂载任何持久文件系统，不包含mkfs/fsck/分区修复或重置命令。
3. 审查内核自动持久写入路径与真实block只读保护，不仅靠shell不调用dd。
4. 输出通道有界、只允许身份/日志/选定TZ读取，不开放任意设备写接口。
5. 启动前核验设备serial/产品/分区边界和镜像SHA，不猜TZ槽位或分区大小。
6. 取证后用保留的iOS镜像临时返回，不刷boot，不改变iOS根或校准数据。

**这只是待审查方向，没有“可安全启动的取证镜像”**。最小collector读TZ
不代表得到原厂Android的ZAP成功日志；它只可能补secure静态分析材料。
用户重连手机后也不能直接启动上述被拒绝的boot。#63/#64仍受证据缺口阻塞。
