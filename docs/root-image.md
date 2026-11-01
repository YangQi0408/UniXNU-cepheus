# iOS 根镜像：恢复期文件与离线验证

## 当前证据

小米 9（cepheus、8 GB / 256 GB）已运行到 iOS 用户态；没有主屏幕成功证据。
首次本地重建镜像漏了 ios-sweet 的恢复期 keybag 初始化。
屏幕明确显示 `systembag.kb: No such file or directory`、`FATAL KEYBAG ERROR: kb_load`，
随后请求 recovery 重启并在 `IOPlatformExpert.cpp:884` 重启超时。
USB 枚举仍存在但线程已停止响应，与停机过程相符；不能把它推断成 SpringBoard OOM。

`scripts/prepare-ios-root.py` 与 ios-sweet `tools/mkiosroot.sh` 的恢复期文件一致：

- 无密码 AppleKeyStore shim 的 `KeyBagKeys = b"SWEET"` systembag plist
- keybag 文件/目录 root:root 0600/0700
- logd 三个目录 diagnostics、uuidtext、diagnostics/timesync 的 numeric owner 272:272
- 三个空 `.migrated` 文件，272:272 0644

这是研究用 shim，并非 Secure Enclave，也不是正常 iPhone 的加密 keybag。
helper 不覆盖已有 keybag，但 verify 会拒绝不符合这个 bring-up 格式的内容。

之前镜像中出现过 cache 数据 fork 大小为 0 的记录。HFS+ 硬链接本身可以有空
占位记录，简单解析器未必能解析实际 inode；不能只凭这种记录认定上游 APFS
读取失败或确定 SpringBoardUI 断言的因果关系。本地重建使用独立副本，实际读
cache 头及检查文件大小，并在 fsck 之后再次验证。

## 工具容器

在仓库根目录：

```bash
docker build -f build/Dockerfile.apfs -t apfs-tools build
docker build -f build/Dockerfile.root -t root-tools build
docker run --rm -v "$PWD/scripts:/scripts:ro" root-tools \
  python3 /scripts/test-prepare-ios-root.py
```

apfs-fuse 固定为本地验证的 `66b86bd525e8cb90f9012543be89b1f092b75cf3`。
根脚本和 tweaks 参考 ios-sweet `45263bee51df4a0f55cdf9c649af886399488ddb`，
原作者许可证见 `licenses/ios-sweet-MIT.txt`。
容器的 privileged 和 /dev 暴露仅用于 loop/FUSE，运行的是本仓库脚本，具有主机级风险。

## 最快修复：保留源 raw，只修改副本

```bash
docker run --rm --privileged -v /dev:/dev \
  -v "$PWD/scripts:/scripts:ro" -v /path/to/local/images:/images \
  root-tools bash /scripts/repair-ios-root.sh \
  /images/ios16-root-new.img /images/ios16-root-keybag.img
```

脚本拒绝覆盖源/已有输出；生成 raw 和 sparse。写入后卸载，执行只读 fsck，
再只读挂载核对 keybag 内容和权限、logd 标记、44 个非空 cache 的文件头/总大小、
launchd、dyld 和 SpringBoard 可执行文件大小。
不重新应用激活或音频 tweaks，不改内核。**离线验证不等于启动成功。**

## 从用户自行取得的 IPSW 完整构建

组件名固定用于 iPhone X iOS 16.7.16（20H392）：
OS `087-86698-044.dmg`、SystemOS `087-86593-047.dmg`、AppOS `087-86715-047.dmg`。

```bash
docker run --rm --privileged -v /dev:/dev \
  -v "$PWD/scripts:/scripts:ro" -v /path/to/ipsw:/ipsw:ro \
  -v /path/to/output:/out root-tools bash /scripts/rebuild-root.sh \
  /ipsw/iphone-x-16.7.16.ipsw /out/ios16-root.img 16G
```

文件从 APFS 解压到 HFSX，因此磁盘空间需要约数十 GiB。
脚本拒绝覆盖已有输出、检查恢复期文件和 cache，并在失败时避免删除仍挂载的目录。
仓库不包含也不发布 Apple 根文件系统。

## 再次启动和日志

保留已进入用户态的 `boot-cepheus-cpu.img`：8 核、`sweetusb=2`、输入关闭、屏幕 console 开启。
CPU 的 ADT 节点保留真实 `qcom,kryo485` 并追加 `SweetCPU` personality 所需的
`qcom,kryo470`；否则 CPU 驱动不匹配，早期 `ml_wait_max_cpus()` 无法结束。
`CEPHEUS_BUILD_STAMP` 可显式标记构建，不指定时使用固定 `sweet-shim`。

开机前两个终端：

```bash
sudo bash scripts/watch-usb-log.sh
python3 -u scripts/udplog.py /path/to/keybag-boot.log
```

watcher 根据 MAC `02:00:5e:77:ee:01` 配主机 `192.168.7.1/24`，
手机地址 `192.168.7.2`，UDP 5140。网卡重连/地址被清除时重新配置。
持续观察者 Ctrl-C 停止，不要运行宽泛的 `pkill -f`。

在确认设备、备份后：

```bash
fastboot flash userdata ios16-root-keybag.simg  # 会清空 userdata
fastboot boot boot-cepheus-cpu.img              # 不写 boot 分区
```

校准分区不可重建，先备份 persist、modemst1/2、fsg、fsc。不要刷这些分区。
刷 iOS userdata 后不能承诺重启即回到正常 Android。

若再次无法接收日志，在可信 root recovery 的 ADB 中：

```bash
bash scripts/pull-log.sh /path/to/kernel-console.log
```

它只读 userdata 末尾 1025 个 4096 字节块，检查 `SWEETLOG` 头并处理环形回绕。
无需格式化、解密或挂载 userdata。无法保证 panic 前的最后一秒已写入持久化区。

## 修正版首次真机测试（2026-11-01）

root sparse SHA256：`313d5ccd792febf65fed401a17a54ca67591d55125b28b6289ea62bcccb53dde`。
保持 boot SHA256：`e933b355b2f60ea842bfa30f5f8d66d2bfa9d93c6f80cb2f9c438e60aedf46e6`。
fastboot 20 个 sparse 分块全部 OKAY，随后 `fastboot boot` 成功；未写 boot 分区。

日志证据：

```
SWEET CPU: 8/8 CPUs initialized, SGI1 verified
MKB_INIT: System Keybag loaded
****** DONE MKB_INIT ****
exec SpringBoard
SWEET SURF: backboardd create id 2 1125x2436 ... -> 0x0
SWEET FB: swap 1 layer 0 surface 2 ...
SWEET FB: swap 7 layer 0 surface 3 ...
SMP SpringBoard[26] ...
```

已收到超过 150 KB UDP 日志；ping 3/3 成功。观测窗口内 SpringBoard exec 仅一次，
未见之前的 SpringBoardUI 断言或 keybag panic，但 bluetoothd / fairplayd.H2 有重复 exec。
有显示 surface/swap 不等于已经到主屏幕，尚需目视确认和输入测试。

### console-only 照片后的调查

用户照片只见 console，无 panic。继续抓取同次启动日志（本地 `keybag-boot.log`）：
前七次采样 SpringBoard 主线程地址不变，ipsw 3.1.741 用同版 IPSW 的 cache
和运行时 slide `0x44830000` 符号化得到：

| 运行时地址 | unslid / 符号 |
|---|---|
| `0x206e41ad0` | `0x1c2611ad0` / `__ulock_wait` |
| `0x216338140` | `0x1d1b08140` / `os_unfair_lock_lock_slow` |
| `0x1c64b97b8` | `0x181c897b8` / `_LSContextInitCommon` |
| `0x235428c64` | `0x1f0bf8c64` / CCSModuleRepository filterModuleMetadata block |

随后同一 PID 26 产生 UIKit layer 日志、新 surface 和新的 swap。
第八次采样主线程变为 `CFRunLoopServiceMachPort -> GSEventRunModal ->
UIApplication _run -> SBSystemAppMain`（运行时 `0x203765998` / `0x1cd92434c` /
`0x20a0955e8` 对应后三个符号）。不能据早期锁等待认定为永久死锁。

PosterBoard PID 396 有 SIGTRAP；PC `0x205fa8268` 对应
`-[PBFPosterExtensionDataStore _stateLock_setupSwitcherConfigurationForFirstTime] + 2040`。
尚不清楚触发条件。没有足够证据认定主屏幕已经显示或渲染地址正确。
REA 打开了提取的 ControlCenterServices（SHA256
`d7e7925d17198475a5bd7969b883173751778618b87e374a1e8daaf9e967ad07`），
函数分析调用超时；上述符号证据来自 ipsw CLI，而非成功的 REA 反编译结果。

先做最小对照：`scripts/make-console-boot.py ORIGINAL QUIET --quiet 1` 仅改
payload 配置中的 `sweetfb_noconsole=1`。保持内核/ADT字节一致，并保留原
Aloha 的 8 字节 boot id 格式（更新 payload digest），不改扩展命令行。
`scripts/test-console-boot.py ORIGINAL QUIET` 校验这几个不变量。
quiet boot SHA256：`26f14dfbbf60984b4260673cab2c845dab0c59a5a3805380fe1fe0a2c67b7373`。
尚未启动此镜像；下一步只需临时 fastboot boot，无需再刷根镜像。
