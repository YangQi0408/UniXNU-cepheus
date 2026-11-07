# Cepheus PILDxe 静态取证（2026-11-07）

## 范围与结果

继续PAS13调查，仅检索已有本地材料及反汇编既有Cepheus UEFI原件。
**未重启手机、未发新SMC、未认证/复位/电源写入，未改变镜像或内核门禁。**
本轮没有取得本机Android ZAP成功记录，也没有取得secure world的PAS13
dispatch实现。任务#63仍未完成：不能把UEFI客户端代码当secure实现，不能
把文件存在当认证成功。supported7=false原因与真实PAS13认证能力仍UNKNOWN。

## 本地材料检索（有界阴性结果）

- 完整检索`../logs/`当时的425个`.log/.txt`文件，合计640168974B，
  大小包括CI与离线日志；大小写不敏感模式
  `a640_zap|Brought out of reset|scm_call failed: func id`无命中。
  这是这批已有项目日志的检索结果，不证明手机历史上从未成功启动ZAP。
- 检索项目inputs/backup、本地Android vendor、EDL boot备份及Downloads等
  指定目录的tz.mbn/tz.img/hyp.mbn和dmesg/logcat/last_kmsg文件名，未找到
  可归属本机的secure固件或ZAP运行记录。不是全磁盘穷尽搜索；未检索私人
  浏览器数据，未读取persist/modemst/fsg/fsc等校准备份内容。
- 已有EDL备份仅boot/kernel/DTB/ramdisk，不能提供secure dispatch实现。
- 本地Infinity-X Cepheus ZIP的中央目录有27项，没有tz/hyp独立固件成员；
  是第三方ROM，并非原厂secure固件，未解压vendor数据，不推断其内部没有
  所需文件，更不把ROM存在视为本机GPU运行证据。

## 原件与REA身份

对象：
`../uefi/Platforms/SurfaceDuo1Pkg/Device/xiaomi-cepheus/Binaries/QcomPkg/Drivers/PILDxe/PILDxe.efi`。
ARM64 PE/EFI Boot Service driver，106496B，SHA256
`851b2a49dfbf7a053b34b8f77a75c51cfbfc6440e0462fbafa95b3636d9d5b71`。
这来自现有Cepheus UEFI树，不是本轮从手机读取的TZ分区，也不是TZ实现。

REA 6.4.0 / Ghidra 12.1.2对原件建立临时只读源映射，分析后已close。
保存完整证据：`../logs/pildxe-static-20261107/evidence-with-mappings.json`，
101974B，18个成功证据记录，SHA256
`06ad04cd713b0b548d2287fe6a303530c1f1a9ee47e4b3dc171954a7d2c8de92`。
先前15-record导出也保留为`evidence.json`，未覆盖。
bundle的unknowns=0只表示没有存储unknown对象，**不表示语义全部恢复**。

## 已观察的客户端控制流

### 1. “not supported in retail”是本地名单判断

`0x11881`字符串引用到`0x1898`，所在函数`0x17c0`遍历
`DAT_00016528`数组/`DAT_00016524`长度，把加载对象与列表条目比较；不在列表
即打印“not supported in retail”并返回5。随后才创建context和调用加载ops。
这是UEFI客户端的local allowlist路径，不是supported7 secure回复。

- xref：`ev_6446d416634f9b740bb8b0251002bcc463bd360eff9f19654a93cd64e51224a9`
- 函数伪代码：`ev_e436cf8e68019f8e687bb29e0d0f9f694560b94661eac799db29ceaa973db47f`

伪代码所示加载顺序为LoadMetaData→ValidateMetaData→SetupMemoryRange→
LoadSegments（若有）→AuthAndReset→post-load，逐回调返回值检查。
禁止修改该allowlist来“制造”PAS13支持；allowlist缺GPU也不证明Android不支持。

### 2. 原件的ELF加载ops映射到PAS1/2/5

`0x11ab8`的三表指针为`0x11800/0x11780/0x117c0`；已读取的
`0x11780/0x11800`两张表均把ValidateMetaData、SetupMemoryRange、
AuthAndReset对应到`0x206c/0x217c/0x2e68`。

- 指针读取：`ev_f51a1369f82f2161eb39b62d5be35070ba16c8d5aaa8a38a0d46c7352fb0601f`
- ELF表读取：`ev_bf3fdb2706eaac7e587138c82f950ed9e041f3db8339dc6481c048cf11afe0a9`
- 另一表读取：`ev_47af97fb8c97c3bbd73ff10b63aafe56a89686fa667b6b043bef87bd276c970e`

`0x206c`在ELF header检查后用`0x02000201/arginfo0x82`调用`0x1e8c`；
`0x217c`设置/对齐加载区域后用`0x02000202/arginfo3`调用同一helper。
命令来自静态客户端代码；本轮没有执行。

- PAS1伪代码：`ev_75a32ea59fbb090650099a9235202b0f6bbb449580b7841a83184b49c585fde1`
- PAS2伪代码：`ev_cb03720d5e12bb98a90156eec5a3842a2fea3b80f8c242175e310b7874c2e3cf`

`0x2e68`没有被Ghidra定义成procedure，decompile请求明确失败，**没有伪造
该函数伪代码**。改为REA读取156个原字节，LLVM离线反汇编显示：
`0x2e80`从context+0xec读取subsystem ID，`0x2e84/88`拼0x02000205，
`0x2e8c`arginfo1，`0x2e90`调用helper，返回EFI错误时打印PAS auth失败。
此处不是固定ID13，只是在调用方读取配置ID。

- 原字节：`ev_8922ed1ae6e1c0195464c83b5047cc2bb22e33c241e08cf4c90d50f522d2e4a7`
- 原文件映射：`ev_16fb4b5c440f3a3a717709a85ab785225f15eb77ff3907459bc6c2eb88527545`
  （offset11880）；与原件相应156B逐字节一致。

### 3. SMC64 helper不是XNU运行时接口

`0x1e8c`收集最多10个参数，定位SCM协议（Boot Services +0x140），
`0x1fa8`OR SMC64 bit30，经协议+0x38传参数/结果，记录调用耗时并返回
协议EFI_STATUS。helper没有读取结果buffer做Linux式`desc.ret[0]`判断。
因此它的EFI返回处理不能照搬成XNU“两层返回”的成功条件。

- helper伪代码：`ev_bbcdab6acca63edd27a168db7f23fda0eeb60e4b1d7dfca2c0cf16b6ab96d452`
- helper汇编：`ev_9b9e41de351d04a2dc0da001c3b2b0e446ddc852d611d2f459bcf4614d3d3db9`
- SMC64指令原件映射：`ev_361bfa05488a7f53873d3d872f391f3bd3bc842c88e61d7b40bfea7942babc2f`

helper的已分析直接引用只有`0x2138/0x2418/0x2e90/0x2f54`，离线核对
命令分别为PAS1/2/5/6，没有该helper上的supported7前置调用。
`ev_3b31e4b575acea7255820d2a72068ceb7e8b6db151a4d2a266cbb32ca9ae4709`。
间接调用或其他传输路径的完整性未知，不能称整个固件绝无supported7。

## 未解除的阻塞与所需新材料

此次仅把UEFI本地allowlist和secure supported7分开，补上了实物客户端证据；
**不是本机PAS13认证成功证据，不能解除supported拒绝分支**。

要继续回答false原因，需要至少以下之一：

1. 已有、本机归属明确的原厂Android启动dmesg/日志（包括a640_zap/PIL结果），
   或与当前手机固件版本可核对的TZ原件，供静态分析。
2. 用户另行明确允许一次受限只读取证：临时恢复Android或recovery环境后，
   只抓日志/读取选定TZ原件；不刷分区、不写校准区、不发送试验SMC或改电源。
   启动Android本身会执行其正常驱动加载，不可称为完全“无硬件状态改变”。

在用户决定前不自行重启。PAS专用内存、crypto租约和stop契约仍需补齐；
触摸间歇性失败根因也未解决。已有32项离线PASS记录仍只是上一轮范围，
本轮文档/静态取证没有新增XNU构建或硬件验收。
