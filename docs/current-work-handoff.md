# 小米9 iOS移植：当前全部工作交接

更新：**2026-11-08**。范围：独立ZAP内存工作区与此前七次SCM查询验收。
这是当前状态总览；各专题文档保留按时间追加的历史记录。

最新离线实现独立ZAP工作区：1MiB连续/PA对齐镜像+16KiB MDT预算，固定快照
持有到复制完成、全容量清零、只读lease、与GPU池分离的handle和预算、局部
失败清理及complete失败永久保留。5种真实材料场景ASan/UBSan、相关GPU内存
host套件、9项integration、PA mask及修改C++静态分析通过。完整XNU构建待验证。
证据../logs/zap-memory-20261108/；普通Normal-NC内存仍没有secure/XPU保证，
禁止送PAS。下一缺口为secure内存/cache合同、crypto资源及真实认证/停止后端，
同时GPU电源/SMMU所有权未完成。没有新SMC或手机操作，不追加触摸轮。

最新离线153561e已补齐ZAP ELF不可变快照及内核MDT/重定位镜像准备。
本机MDT6860B、加载段1968B与原件逐字节一致，逻辑镜像4096B、未来分配需求
1MiB分开保留；不提供PAS PA/XPU保证、不认证、不置zap_loaded。ASan/UBSan
相关内存生命周期及新路径、9项integration、修改生产单元静态分析通过；
未重跑完整离线入口或追加手机测试。CI42457537126 @153561e完整成功，kernel
SHA763d274e.../17414848B，76核心入口/13内存API链接、无unresolved、staged source/
触摸/PA64核验PASS；产物../inputs/ci-42457537126/kernel.macho，未打包/boot。
手机仍运行已验收b2b99e7查询版，证据../logs/zap-staging-20261108/。
详见[ZAP准备实现与缺口](zap-platform-contract.md)。

最新query7-basic-b2b99e7（SHAa060bd43...）临时boot后，sample25四组及七次
REGS in/out/summary/QUERY7/end均齐全。生产策略核验TRANSCRIPT_COMPLETE，
command7 availability=0/1，QUERY7 confirmed=1，PAS13 supported仍0/0=false。
convention2/available0x1f/calls7/transport0/secure0/e-1；不代表认证失败或成功。
用户“正常”确认查询后触摸/唤醒PASS；查询后9条health events78增至170/errors0，
电源、blank/display on及后续触摸事件收到。RAW128，48完整批123事件0差异。
420.000939s/495包/164342B，日志SHA12dda1d3...，采集14:43:41自然到期，
回复观察14:43:57，未实际提前停止。自己的capture/watch均退出，手机保持运行。
证据../logs/query7-basic-b2b99e7-live-20261108-1436/result.json及scm-query.json。
用户时间紧，本轮不追加真机测试，不重复控制/完整离线；下一离线重点为
GPU电源/共享资源、SMMU及PAS加载后端和false差异，不放宽门禁。
GPU/Metal尚未运行；本轮排除command7不可用，触摸间歇和PAS13=false根因未解。

前一14:27基础控制轮soccontrol-b2b99e7本轮PASS：用户“都正常”，420s648包/
186771B，sample25四组齐全且之后触摸events317增至890/errors0，HID69对。
日志SHA2a4a5873...，证据../logs/soccontrol-b2b99e7-live-20261108-1426/。
自有采集/watch均退出。用户明确时间紧、尽可能省测试：不重复控制/离线回归，
查询出完整结果再一次简短点按/唤醒确认即收尾。配对query7-basic随后已完成，
见上述结果，不再重复控制。

最新指示“直接下一阶段吧”：转入基础SoC采样后的SCM/PAS13固定查询准备，
不再以补跑240s时序对照阻塞下一阶段；历史缺证据仍不追认PASS。
复用CI42450429814/b2b99e7 kernel SHAe9f8f3f7...，两张同核镜像已打包、
严格delta核验：soccontrol-b2b99e7 SHAb239af44...（只加cephsoc1，之后本轮PASS），
query7-basic-b2b99e7 SHAa060bd43...（相对控制只加cephscm1）。
默认45s基础等待/sample25后查询，不带fuse/ARC/GX/boost/memory/240s延迟。
新增基于生产策略的RAM日志核验13项、参数15项、时间线6项测试通过；
完整离线38/0/0，receipt ../logs/gpu-offline-query7-basic-20261108/。
新基础控制及配对查询现均已完成，手机保持查询版iOS运行。
GPU/Metal尚未运行，细节见[SCM查询结果](scm-query7-preflight.md)。

最新14:02轮：同核延迟`socdelay240-b2b99e7` SHA7e082de6...临时boot成功。
用户“现在正常”及两次复测“正常”确认当前UI/中央触摸/电源唤醒触摸PASS；
确认正常控制iOS经物理电源+音量下进fastboot，初始化等待未碰屏幕。
但USB14:04:28及14:07:24重新枚举，末UDP14:04:28，随后0有效RX且错误
增长；用户未主动拔插、怀疑线材，原因UNKNOWN。执行环境重启后capture
signal提前结束469.53s/600s，314包103834B，日志SHAe89b06c2...；自有
capture/watch均退出。无scheduled/begin/baseline-end/sample25，**阶段
对照INCONCLUSIVE，实际SoC执行UNKNOWN**；初次回复观察晚于末UDP，
不确认探测前PASS。RAW128早达限，40完整批126事件重放0差异；相关16测试
通过，单条health早于中央动作。证据`../logs/socdelay240-b2b99e7-live-20261108-1401/`。
保留当前可用手机，不自动重启/恢复；当时建议稳定数据线后补跑，用户后续
改为下一阶段，见上。GPU/Metal仍未运行，根因UNKNOWN。

前一真机13:52轮：新核`touchcontrol-b2b99e7`临时boot，用户“都正常了”确认
主屏幕/中央拖点/边缘/双指/电源熄屏唤醒触摸PASS。前序为失败iOS经物理
电源+音量下进fastboot，未报告中途Android。420s589包/157985B，日志SHA
d0722828...；last health18000/events1179/hid_calls1172/errors0，26完整批/
120事件重放0差异（RAW预算128尾批省略）。采集与自有watcher已结束，
ECM仍在，未自动重启；当时延迟候选未启动，之后本轮结果如上。
证据`../logs/touchcontrol-b2b99e7-live-20261108-1347/`。间歇根因仍UNKNOWN。

最新离线推进：新增固定`cephsocdelay=240`基础探测门禁及scheduled/begin/
baseline-end内核uptime标记，默认45s不变；不新增硬件操作。生产线程host
测试、4项封装门禁、14项参数及原SCM测试通过，完整离线37 PASS/0 FAIL/
0 SKIP，提交b2b99e7干净树重跑同样通过，receipt
`../logs/gpu-offline-socdelay-b2b99e7-20261108/receipt.json`。
CI42450429814 @b2b99e7完整成功，kernel SHAe9f8f3f7...；新核touch-only
`boot-cepheus-touchcontrol-b2b99e7.img` SHAafdf72c6...，同核延迟版
`boot-cepheus-socdelay240-b2b99e7.img` SHA7e082de6...，严格配对核验通过。
当时两者未测；之后新核控制PASS、延迟版当前体验PASS但阶段对照无结论。
流程见[soc-delay-preflight](soc-delay-preflight.md)，根因仍UNKNOWN。

最新12:35轮：`soconly-ordered-60a2dc5`临时boot，触摸FAIL，用户确认中央
操作已做、主屏幕/电源正常，选择“保留当前现场，先离线排查”。420s452包/
123804B，19条health至19000保持events6/hid_calls2/errors0，仅1079,0边缘
enter/leave；sample0..25四组齐全、GMU跳过，无SCM/GX/fuse/ARC/boost/内存。
日志SHA `a929e459341d37375bd4f72452b5a9e98baa3f8a9452668b8f03713adf1ee566`，
证据`../logs/soconly-ordered-60a2dc5-live-20261108-1235/`。当前ECM仍在，代理
未重启/恢复；停止后续GPU探测。6事件生产重放0差异，批内有序上报没有
解释无中央FIFO。离线包时间线4项测试通过，生产kernel/镜像未修改。
**根因仍UNKNOWN，不根据单次跨启动对照认定cephsoc单项因果。**

先前01:10轮：`touchordered-60a2dc5`临时boot本轮用户确认“好了，都正常”，主屏幕/
触摸/电源熄屏唤醒后输入PASS。日志575包/161064B，最后health events1148/
hid_calls1101/errors0；完整身份见gpu-acceptance和
`../logs/touchordered-60a2dc5-live-20261108-0111/result.json`。间歇根因仍未知。
采集尾01:15:47 ECM断开后见Xiaomi MTP/ADB，原因待确认，代理未重启。

用户要求继续至下一测试点：同kernel的`soconly-ordered-60a2dc5`已打包并
严格核验仅新增cephsoc=1，11项参数回归通过；SHA
`5d7e64ae5740ad4ab82e2e6ef614e537bd344e483b77aa44f0470cc1f8be8a24`。
该候选随后已测试触摸FAIL，不作为可用基线。详见
[同核SoC基础预检与结果](soc-ordered-preflight.md)及
[触摸理论最新排查](touch-failure-theory.md)。下面保留各阶段历史上下文。

前一离线阶段（历史）：用户要求“继续推进，直到需要在真机测试”。已实现整批先验证、
按FIFO顺序上报的独立修正；11项重放测试及生产FIFO故障测试通过，10份
保存RAW与顺序模型0差异。完整离线回归34 PASS/0 FAIL/0 SKIP，
receipt为`../logs/gpu-offline-touch-ordered-20261108/receipt.json`；
XNU CI42391773972 @60a2dc5完整成功，内核/PA64/linked核验及新旧touch-only
镜像打包完成。新候选`boot-cepheus-touchordered-60a2dc5.img` SHA256
`a00d1e29087d0f28e695eb0ec91166c48a917fde45685b655c46162541c9f074`，未启动；
触摸间歇根因未解，不把离线短点修正称真机已恢复。候选首轮仅触摸，
SCM/CPU boost/内存/GPU探测关闭。详见touch-failure-theory最新推进段。
当时停在真机前；之后验收已在本文开头更新。完整镜像/证据及分轮验收见
[有序上报真机前检查](touch-ordered-preflight.md)。

2026-11-08用户已拿走手机，要求先做理论。10份已有RAW日志的host重放
确认生产驱动按整批最终状态上报会吞掉批内enter→leave；最新失败19事件
可复现只有2组down/up，顺序模型5组。**这是独立源码缺陷，不能解释后段
空FIFO，间歇根因仍UNKNOWN**。新增8项host测试及CI/离线入口，生产驱动
与镜像未改，无新XNU构建/远端CI/手机操作（该段为先前理论轮）。详见
[触摸失败理论与重放](touch-failure-theory.md)。下面硬件结论均为历史记录。

> **最重要的现状：iOS 16.7.16 曾多轮成功运行主屏幕、触摸和电源键；
> 有序touch-only及新核控制PASS；历史45s soc-only触摸FAIL保留。
> 历史240s延迟轮体验PASS，掉线/中断导致阶段对照INCONCLUSIVE。
> 最新基础控制及QUERY7查询后触摸/唤醒PASS；七次查询完整，command7可用，PAS13=false。
> GPU/Metal硬件加速没有完成，未尝试PAS认证。
> 触摸间歇失败根因和PAS13 supported=false原因都没有定位。**

## 1. 目标、设备和证据分级

目标是在小米9上临时启动iOS 16.7.16，保持可交互SpringBoard、物理输入和
熄屏功能，再依次打通Adreno640平台、受限命令执行及iOS图形接入。

| 项目 | 已固定信息 |
|---|---|
| 设备 | Xiaomi Mi9，`cepheus`，SM8150 / Snapdragon855 |
| 预期serial | `67eea163` |
| 内存／存储 | 实测8GB／256GB机型，不泛化到其他布局 |
| 面板 | Samsung EA8076，1080×2340 |
| 触摸 | ST-FTS，I²C `0x49`，chip ID `36:39` |
| GPU | Adreno640，FDT chipid `0x06040001` |
| iOS | iPhone X材料，16.7.16 / 20H392；研究用SWEET shim |
| 最近设备观测 | query7-basic七次查询完整，PAS13=false；查询后触摸/唤醒PASS，采集已结束，手机保持运行 |

证据分为四级，**不得互相替代**：

1. **离线证据**：源码、固定固件字节、布局解析、RAM mock及单测。
2. **构建证据**：实际XNU编译／链接、符号和产物核验。
3. **真机局部证据**：寄存器／返回值／内存pattern／事件日志，仅限相应场景。
4. **用户验收**：指定镜像、指定启动的主屏幕／触摸／电源体验，不等于长期稳定。

GPU识别、命令编码、编译成功、SMC返回或CPU内存自检都不是GPU加速成功。

## 2. 工作目录与提交边界

所有下面的绝对路径均位于 `/home/yangqi/cepheus-port/`。

| 路径 | 用途／注意事项 |
|---|---|
| `gpu-work/` | **当前实现和证据文档工作树**，分支 `feat/adreno640` |
| `cepheus/` | 原主树，仍为 `35561c0`，有未提交SoC草稿及未跟踪文件；不要reset/clean覆盖 |
| `reboot-work/` | 已取消USB重启工作的隔离树，回退到 `1b59abd`；不继续此功能 |
| `dist/` | 本地启动镜像、打包receipt、FD及根镜像链接，旧产物全部保留 |
| `inputs/ci-*/` | 按CI运行号保存内核、摘要及固定源码commit |
| `logs/` | 实测日志、失败记录、离线receipt、私有静态取证输出 |
| `root/` | HFSX根镜像构建／修复材料；`dist/ios16-root*.simg`是链接 |
| `uefi/`、`unixnu/` | 原启动链材料，不直接覆盖已有脏树 |
| `upstream-sync/` | 独立上游候选树，不能默认晋级为已验收固件 |
| `inputs/gpu-refs-20261106/` | 固定HackiOS、Pong及GPU子模块参考源 |
| `/home/yangqi/android/kernel/xiaomi/sm8150/` | 本机下游源码参考，固定HEAD `36f369fdb77319a2984a48b89f025ac3e5f97386` |

当前生产改动已推送为 **`153561e`**，新增ZAP内核准备路径，CI42457537126
完整构建及产物核验成功。手机已验收镜像仍对应 **`b2b99e7`** / CI42450429814；新的源码或
文档提交不改变已验收镜像身份。65f5b96记录此前完整QUERY7实测。

最近提交主线：

| 提交 | 工作 |
|---|---|
| `008b2cd` | GX诊断偏移等修正，不是触摸根因修复 |
| `a0fa7a5` | 显式boot opt-in、受限one-shot SCM/PAS信息查询 |
| `2b2ed98`、`bf58eb2` | 固定六次SMC真实返回、PAS13=false及查询后UI验收记录 |
| `a49d4e8` | 原厂PAS13加载路径审查，撤回过强不可加载推断 |
| `5d0d2d7` | PILDxe客户端静态证据与本机日志缺口 |
| `cbc4750` | 拒绝有formattable风险的Android取证boot |
| `41ec3b1` | 本机TZ原件的原生Ghidra PAS查询链证据 |
| `da8ffc1` | TZ入口／重定位核对、补查command7 availability策略及测试 |
| `0ffbf3c` | 历史SCM关闭基线触摸FAIL，停止当时QUERY7开启测试 |
| `60a2dc5` | 整批验证后按FIFO顺序上报触摸 |
| `b2b99e7` | 可选固定240s基础探测等待；本轮复用该内核的默认45s配置 |
| `9594ed7` | 基础SCM配对封装及生产查询策略日志核验工具 |
| `a688daf` | 新基础控制PASS，缩减后续真机检查 |
| `65f5b96` | QUERY7七次完整、PAS13=false及查询后触摸/唤醒PASS |
| `153561e` | 固定ZAP快照、MDT及重定位CPU镜像准备，未认证/加载 |

## 3. 启动链与iOS用户态：已经做成的部分

启动链为：Android boot容器 → Project Aloha UEFI → UniXNU FAT payload →
DarwinBoot → XNU → userdata上的HFSX根 → launchd → SpringBoard。

- 用本机FDT和ADT替换参考机板参数、内存／UFS布局及面板信息。
- UEFI FD严格3MiB。扩成4MiB会覆盖SEC heap、vectors、页表和stack区域；
  不通过扩大固件区来解决构建空间问题。
- CPU personality匹配修正，8/8 CPU初始化和SGI1有日志证据。
- 修复恢复期 `systembag.kb`、logd目录／owner／`.migrated`，离线fsck与
  实际dyld cache文件头／非空文件核验；真机MKB_INIT及SpringBoard已通过。
- 关闭覆盖画面的framebuffer console，打通一次Home事件进入主屏幕。
- 保留PON物理电源键，去掉参考机错误GPIO按键读取；音量键不能据此认为已完整支持。
- USB CDC-ECM UDP日志可用，研究用持久console环仍保留。

原安装阶段确实重建并写入过userdata；它现在不是Android的正常data文件系统。
`fastboot boot`只说明没有刷boot分区，**不表示运行中的iOS不写根FS或持久日志**。
后续测试无需重新刷root；不要格式化userdata来“修复”触摸或启动。

根镜像历史修正版SHA256：
`313d5ccd792febf65fed401a17a54ca67591d55125b28b6289ea62bcccb53dde`。
它是原修复／安装记录，不是本次重新计算15GB根镜像的结果。
根镜像和Apple专有材料不上传公共仓库／CI。

## 4. 显示、熄屏与CPU性能工作

### 显示与熄屏

- workloop使用固定提交记录，减少每帧动态分配。
- 绝对期限调度，迟到跳过时隙而非追赶忙循环；`sweetfb_hz=30`是请求值，
  不是实测面板FPS。
- 限量记录submitted／presented／overwritten／copy_us／missed／off。
- 去掉读取write-combined framebuffer的逐像素分层淡出；off时仅一次清黑，
  加锁避免过时清黑。历史实测约0.7–0.9ms且用户确认改善。
- 这不是完整DSI／PMIC断电实现；present耗时也不包含用户态软件光栅化全程。

### CPU

- OSM只读实测：L3约1536MHz、Silver约1633MHz、Gold约711MHz、Prime约827MHz。
- 120s试验在固件LUT内选相同open-loop mV候选，将Gold／Prime请求到1.4016GHz；
  真机周期计数约1.402GHz，随后恢复旧请求有日志证据。
- 实现触摸活动驱动的限时CPU floor及TSENS热门槛：65°C退出、低于55°C允许
  重入、活动3s窗口、温度样本过期／无效拒绝。已有策略／温度日志，仍不是
  完整生产governor或异构调度整合，也没有受控持续动画benchmark。
- 最新失败基线**没有开启CPU boost**，不得把它归因于本轮主动超频。

PosterBoard历史SIGTRAP、bluetoothd／fairplayd.H2重复exec等用户态缺陷仍保留
记录，不因主屏幕可见就声明所有iOS服务完整。Wi-Fi／BT／音频／相机／蜂窝、
Apple服务和App Store没有获得可用验收；研究shim不等同SEP或正常iPhone安全模型。

## 5. 触摸工作：有序touch-only通过与最新SoC轮失败

### 已实现及验证

- SE17真实 `FIFO_DISABLE=1`；不能强开FIFO。GPI/GSI DMA仍未实现。
- 独立GPIO开漏I²C：SDA55／SCL56、IRQ122，RESET12／VDDIO84，遵守
  总线busy／仲裁／stretch超时，失败不发恢复脉冲。
- 限定ldoc1 XOB enable-only请求、正确电源地址，不改电压或校准分区。
- 原厂ST-FTS enter／motion／leave、首事件保留、跨批次状态、530218
  continuation规则修正；后者有源码和测试依据，但未证明是间歇失败根因。
- 1080×2340触摸canvas映射到SweetHID 1080×2400。
- opt-in诊断限128个非空RAW及30条health；SYSINFO用A6/0000只读已加载内容，
  不发load-data、固件或校准命令。
- touch-power旧基线及后续touchonly → soconly → socfuse → socfusearc →
  新核GX关闭 → 新核GX开启多轮分别获得用户正常确认。

### 最近失败轮：不是新SCM查询造成的

2026-11-07临时启动 **`boot-cepheus-allprobes-da8ffc1.img`**：

- `cephscm`不存在，CPU boost和内存workspace自检不开。
- 芯片36:39／SYSINFO可读，scan发送err=0；不等于扫描及交互正常。
- 起初19个非空事件：ready1／status3／enter5／motion5／leave5。
- 五个enter原始x均1079（右边缘），不是HID缩放制造的坐标。
- 全部16条health从1000到16000轮询保持events19／hid_calls6；最后
  reads16000、empty15994、errors0、active0、skip_next0、IRQ/SDA/SCL=1。
- 有touch HID down2／up2，但用户明确“触摸是坏的” → **验收FAIL**。
- 43:05:01／43:05:30对应下游force-cal状态；43:05:30在正常旧轮也出现，
  不是充分因果证据、不是可以改校准的理由。
- 与旧touchdiag失败表征相似。触摸源码blob及基线参数与a0fa7a5相同，
  仍不能排除内核时序／布局、电气状态、继承状态或固件内部行为。

准确结论：驱动能读芯片和FIFO，但捕获后段没有新的非空事件；根因**UNKNOWN**。
没有同步中心触摸时间轴，不能单凭空FIFO断言控制器停扫。既往多轮通过不等于
间歇问题修好，也不证明这次所有触摸硬件损坏。

没有自动复位、重发scan、固件／校准修改或试写电源恢复。手机未由代理重启。
该历史轮之后曾有有序touch-only验收，45s soc-only再次失败，当时用户选择
保留现场、先离线排查。之后经明确授权新核控制PASS、240s延迟轮体验PASS
但阶段证据不足；当前状态与后续门禁以首页及soc-delay-preflight为准。

## 6. GPU源码成果与真正缺口

下表“已实现”主要是生产模块源码、host故障测试和实际XNU链接；**不是平台
全部接通**。当前镜像没有GPU执行startup钩子，未实际发起GPU供电／固件／CP。

| 子系统 | 主要源码（`xnu/`） | 已有证据／仍缺什么 |
|---|---|---|
| SoC／电源门控 | `cepheus_soc.c`、`cepheus_gpu_power.h`、`cepheus_gpu_power_txn.h` | GPUCC/CX gate真机拒绝分支；真实ARC／clock／GDSC所有权后端仍缺 |
| ARC／BCM／配置 | `cepheus_arc*.h`、`cepheus_bcm.h`、`cepheus_gpu_bw.h`、`cepheus_gpu_config.h` | cmd-db/TCS只读及FDT源绑定OPP/BW规划；NOT SENT，不是旧票继承证明 |
| GPU内存 | `cepheus_gpu_memory.cpp`、`cepheus_gpu_memory*.h` | 真实IOKit接入、NC属性／PA门槛、单缓冲pattern回归；不是GPU DMA一致性通过 |
| GMU工作区 | `cepheus_gmu_workspace.*`、`cepheus_gmu_firmware.h`、`cepheus_gmu_upload*.h` | 分块、权限、不可变固件snapshot、6设备页表等CPU自检；未真机上传TCM |
| SMMU／VA | `cepheus_smmu_*.h`、`cepheus_gpu_vm*.h`、`cepheus_gpu_va.h`、`cepheus_gx_smmu_plan.h` | 4KiB设备页表与16KiB主机页划分、role检查、SID/ASID计划；TBU/ACTLR所有权及TLB实测缺 |
| PDC／RSC／QMP | `cepheus_gpu_pdc.h`、`cepheus_pdc_mmio.h`、`cepheus_qmp*.h` | 限定MMIO／握手／租约契约和测试；真实连接／运行租约未成立 |
| HFI／GMU启动 | `cepheus_hfi*.h`、`cepheus_gmu_bringup.*`、`cepheus_gmu_hfi_link.*`、`cepheus_gmu_start.h` | 有界ring、发布顺序、v2消息、CM3/OOB启动和故障模型；未真机boot GMU/HFI |
| GX／CP | `cepheus_gx_*.h`、`cepheus_cp_*.h`、`cepheus_a640_pm4.h`、`cepheus_sqe.h` | halted-window gate、保护、LLC/HWCG/VA、ring／scratch／fence规划；未解除halt／执行 |
| 故障／热／退休 | `cepheus_gpu_fault*.h`、`cepheus_gpu_thermal.h`、`cepheus_gpu_tsens.h`、`cepheus_gpu_retire.*`、GMU park/slumber | 故障隔离／RAM模型／生命周期测试；真实独立热监督、静默DMA及可靠断电／退休后端缺 |
| SCM/PAS | `cepheus_scm_discovery.h`、`cepheus_gpu_compile.c` | 受限原生SMC信息查询已实测；PAS专用XPU内存／crypto租约／auth和shutdown未做 |
| iOS图形前端 | 尚无本机完成的实现 | IOAccelerator/Metal ABI、shader compiler、资源同步、CoreAnimation GPU合成均缺 |

**已修复的真实panic**：`pmap_sync_page_attributes_phys`将32位pp左移后才转宽，
导致>4GiB PA截断。`0088-cepheus-pmap-cache-pa.patch`改用先扩展的`ptoa(pp)`。
源测试、linked 64位UBFIZ核验及真机32KiB NC pattern／release回归支持修复。
保留PA bounds／managed page／有效NC cache属性检查；不把physicalMask曾经
的猜测继续当根因，也不为了通过自检删门槛或强开缓存。

GMU工作区CPU自检曾通过，但同轮触摸失败，因此不能推荐其镜像作为可用版。
GX诊断的word/byte及fault字段缺陷已修，但真机仍被GMU不就绪门禁拒绝，
**GX可读寄存器路径尚未实测**，不能归因其修正解决触摸。

### 仍然阻止真实GPU启动的硬条件

当前基础控制及查询后触摸已独立验收PASS，间歇根因仍未解；不要求重复本轮控制。

1. PAS13查询false的差异、可信SCM共享buffer、PAS专用XPU内存及crypto带宽租约。
2. ARC/DRV继承票、共享clock/GDSC、真实DDR bootstrap带宽租约。
3. SMMU TBU/ACTLR/CB/ASID所有权，cache/TLB/DMA真机一致性。
4. GMU PDC/RSC/QMP/HFI真实后端，GX完整初始化和热监督。
5. 真实stop/quarantine与DMA静默证明；失败后不能盲free或降共享票。
6. 受限scratch/fence通过后才谈绘制；再后才是iOS用户态GLES/Metal接入。

## 7. PAS13调查、TZ取证与QUERY7候选

### 六次SMC实测（a0fa7a5）

64-bit self availability及INFO查询PAS1/2/5/6 availability都返回x0=0/x1=1。
第六次 `0x42000207`，arginfo1／ID13返回 **x0=0/x1=0 → supported=false**。
`e=-1`是引擎拒绝继续，不是认证失败；没有尝试PAS init/mem/auth/stop。
查询后UI该轮用户验收正常。INFO availability of auth不等于执行auth。

原厂KGSL→SSR→PIL-TZ加载路径不把supported7当准入。撤回“false证明PAS13
必然不能认证／加载”的推断，但**本项目门禁不放宽**；认证能力仍UNKNOWN。
PILDxe retail allowlist是UEFI本地策略，不是TZ安全服务实现，不修改制造支持。

### 本机TZ原件

- 用户进入recovery后读取固定 `by-name/tz` → `/dev/block/sde37`，4MiB。
- 读取前／主机／读取后SHA一致，原件SHA见第9节。
- 首读多86B dd统计输出被长度门禁拒绝，保留失败材料；`status=none`另名重采。
- Recovery已有cache/metadata rw且自动解密／尝试mount data失败；只能声明
  代理取证命令无分区写入，不能声明recovery整个运行环境无写入。
- ELF64 AArch64，无section headers；`TZ.XF.5.2-00209`等字符串仅属原件身份。
  采集分区SHA不证明之前SMC时运行的secure image与它完全相同。

REA strings/xrefs超时、CLI无procedure后，经用户允许改用原生Ghidra12.1.2
Headless，私有DB、`-noanalysis`、显式函数范围；不改固件字节。
最终25 seeds／1847指令／16数据段用PT_LOAD原字节、Capstone和LLVM核对PASS。
反编译函数名和手工原型都是推断；failure helper和间接跳转CFG仍有限制。

静态PAS查询：wrapper `0x1c04f444` → verdict `0x1c04dae8` → lazy lookup
`0x1c04db34` → descriptor find `0x1c04d354`。计数15、stride0x238，PAS13
唯一项 `0x1c13f910`；初始化不清ID。**“固件完全没有PAS13”的解释已撤回**。
静态lazy cache为零不是运行时不支持证据。静态表与真机false仍未解释一致。

重定位核对：4584条type0x403的RELA-format记录。真正启动函数入口为
`0x1c03b274`，`0x1c03b3b0`只是循环；循环读target当前槽值、筛选后加偏移，
不是直接读取addend。原槽值全等于addend只属静态事实。
count／cache／descriptor ID不与重定位目标重叠，不能排除其他写入者。
入口ADR与字面量的相等检查不证明物理加载基址；`0x14690160`原偏移
`0x7eb980000`可在启动时更新，**不得按这些地址扫描活RAM**。
NS↔secure frame全程、初始化其他写入和运行映像选择仍未恢复／验证。

### QUERY7七次查询实测

da8ffc1仅新增一次INFO查询：command7 availability。正常64路径由6次变7次，
第6次 `0x42000601, arginfo=1, arg=0x02000207` 确认后才发第7次ID13查询。
新bit4只代表确认可用；false/error/未到该项要看REGS，不混记“不支持”。

- 未确认可用 → 不发PAS13查询。
- 可用但PAS13=false → 继续拒绝。
- 即使PAS13=true → 也不自动认证／上电／CP，不直接把旧false解释掉。
- 最大16次是本地预算，**不能抢占一个不返回的secure调用**。

旧da8ffc1的allprobes关闭基线触摸FAIL，其配对query7-da8ffc1未启动。
新b2b99e7基础控制已PASS，随后query7-basic实测7次完整：command7 availability
0/1、PAS13 supported0/0、available0x1f、QUERY7 confirmed1，查询后触摸/唤醒PASS。
生产策略转录核验通过；排除本轮command7不可用，false根因仍未知。
该查询不能提供运行TZ SHA／实际基址／handler／表内容，也不证明认证能力。

### Android取证预检

本地三份Android boot实际ramdisk都含data/metadata `formattable`，persist无ro
且check；当前userdata为iOS HFSX，**拒绝启动**，不删单个标志就假称安全。
RAM-only最小取证initramfs还没有通过安全验收，也不再是取得现有TZ的必要前置。

## 8. 固定构建、参考项目与取消事项

### 构建

GitHub仓库：`YangQi0408/UniXNU-cepheus`。GitHub访问使用 `gh`。

- `touch-kernel.yml`：`macos-15-intel`／Xcode16.2，目标arm64而非arm64e。
- ios-sweet：`45263bee51df4a0f55cdf9c649af886399488ddb`。
- darwin-xnu-build：`663906528d1cd5ea8fde7de6f16de73db9511549`。
- 最新完整构建成功：**42457537126 @153561e**，尚未打包/boot；最近已实测内核为42450429814 @b2b99e7。
- 逐一核验关键patch未跳过、staged source、linked symbol、无unresolved、
  PA64指令、触摸／QUERY7标记、取消USB代码缺席。
- 主机无mcopy；打包使用已有 `cepheus-build:latest`，`--network none`、当前UID，
  真实mtools。不是虚假成功工具，不借此改系统软件。
- 最近完整离线receipt为QUERY7准备的 **38 PASS／0 FAIL／0 SKIP**，本轮ZAP
  改动只跑相关检查，不追认其已通过整套38项。每个入口可含多个host程序／case。
- TZ mapping／RELA RAM-only单测14项；最终本地手工5项负向篡改／缺范围均拒绝。

上游UniXNU/Aloha/DarwinBoot候选已隔离同步及FD构建；CI和本地FD摘要不同，
未解释bit reproducibility，未混入当前已验收启动链。新A740/其他iPadOS profile
不能冒充SM8150/iOS16适配。两个只读审查子代理30min超时，没有完整报告，
**不能计独立审查通过**。本轮文档整理没有启动新子代理。

### 外部GPU参考

- HackiOS固定 `57722468a3774e2ec60017705ec356ae9b31f40e`：Raphael/SM8150/A640
  平台事实最接近，当前参考HEAD为GLES-only；其失败即假定旧ZAP已加载回退
  不允许复制。旧机报告不等于本机验证。
- UniXNU-pong固定 `e0ee6df109b4b5dc706cac660d4f8210dc911dac`及明确GPU子模块：
  生命周期／用户态分层值得参考，但A730/SM8475与较新iOS ABI不能直接套用。
- 较合理路线：A640平台与受限CP先完成 → Mesa freedreno GLES前端 →
  本机20H392 Metal ABI适配另行推进。此路线尚不是已交付实现。
- APSL/GPL/BSD/MIT及translator许可分别保留，不上传专有固件／密钥。

### 已取消／未启用

- USB自动重启／诊断控制端点：用户取消，已完整回退 `1b59abd`，不复活。
  保留的CDC-ECM日志不是这个被取消功能。
- panic自动回fastboot只完成可行性审查，可靠runtime reset/reason后端未实现／实测。
- “仅一次真机机会”已被用户取消，可多次逐阶段测试；旧标题／文字仅为历史。
- 本机pi自动压缩曾401，已禁用 `compaction.enabled=false`；不改提示词制造取证结果。

## 9. 镜像、内核和证据索引

### 镜像使用状态（全部位于 `../dist/`，相对gpu-work）

| 文件 | 当前判断 |
|---|---|
| `boot-cepheus-touch-power.img` | 历史多轮触摸／UI通过，**恢复用旧基线首选候选**；不是当前启动已恢复或长期保证 |
| `boot-cepheus-touchordered-60a2dc5.img` | 最新有序touch-only本轮用户UI/PWR/touch PASS；保留可用镜像，不证明长稳 |
| `boot-cepheus-soconly-ordered-60a2dc5.img` | **最新临时启动触摸FAIL**，同核仅加cephsoc；保留证据，不当可用升级版 |
| `boot-cepheus-touchonly-control-da8ffc1.img` | 同参数未修正控制镜像，打包通过、未启动；不是allprobes失败轮的相同profile |
| `boot-cepheus-allprobes-008b2cd.img` | 对应轮次触摸UI通过；GX只走不就绪拒绝，不是GX读取通过 |
| `boot-cepheus-allprobes-a0fa7a5.img` | SCM关闭旧轮UI通过；该验收不自动扩展到新核 |
| `boot-cepheus-scminfo-a0fa7a5.img` | 六次查询及查询后UI通过；PAS13=false，不能加载GPU |
| `boot-cepheus-allprobes-da8ffc1.img` | 历史触摸FAIL，不得作为可用升级版 |
| `boot-cepheus-query7-da8ffc1.img` | **旧kernel仅构建／打包、未启动；当前需新kernel逐项重验基线，不能直接跳过** |
| `boot-cepheus-workspace-6f93116.img`、`boot-cepheus-memcheck-6f93116.img` | CPU内存局部回归有通过证据，但后续对应触摸失败；不作可用UI候选 |
| `boot-cepheus-memcheck-9dc6e6e.img`、旧gxprobe/bisect/cephmem候选 | 有已知panic或不完整修复，不按旧README命令再次启动 |
| 其余历史perf/cpu/probe/root/FD产物 | 全保留；状态按专题及每轮receipt核对，不能因存在文件就视为已测 |

上述关键boot镜像都是70680576B；SHA为本次文档整理重新核对值：

| 镜像短名 | SHA256 |
|---|---|
| touch-power | `c8eae5a0abd8dd9f8cd58f954497c0510c576b941b09bc0ef1efb2648718ea88` |
| allprobes-008b2cd | `8e538843092e7101cd882fc544fdf7fa7e77a0c0ce4d1b4e81ac8608c7ddff87` |
| allprobes-a0fa7a5 | `f0517ba41d446dff3d81a6777da243464d541d9b0724392c7691b700d6f15a50` |
| scminfo-a0fa7a5 | `81dae8c4b97684ac0f08dd21142c846b340f6eeafdf5fae7eb0b77a82cfcfb04` |
| allprobes-da8ffc1 | `96fd757773fd876520a7389095258ab3d2da9398e7b451efae1e20029e5cf34d` |
| query7-da8ffc1 | `645e970bdd3a60592b7f28f46b1ba2457ad8559910bf7958f84ecca7f376637e` |

历史da8ffc1内核 `../inputs/ci-42380203110/kernel.macho`：
`6a03e92c8f3faec7d688c85a9ccbf60819cab9c6e615d6fd079aa6ecd3775e26`。
该历史两镜像ADT均 `6416d6d17b1b65fded10a4dfce1fbf0022216e03b013471aecb6db486703a28a`。
同名`.json`只描述**打包当时**状态；hardware_tested=false不否定后续实测，
也不能因为打包PASS而无视后续FAIL。

### 关键日志／receipt（路径相对 `gpu-work/`）

| 路径 | 内容 |
|---|---|
| `../logs/zap-staging-20261108/` | 153561e ZAP CPU准备、本机MDT/b02比较、ASan/UBSan相关验证与构建记录 |
| `../inputs/ci-42457537126/`、`../logs/ci-42457537126/` | 新153561e内核及完整构建日志，尚未打包/boot |
| `../logs/query7-basic-b2b99e7-live-20261108-1436/` | 7次SCM完整、PAS13=false、查询后触摸/唤醒PASS |
| `../logs/soccontrol-b2b99e7-live-20261108-1426/` | 配对基础控制PASS，无需重复 |
| `../logs/gpu-offline-query7-basic-20261108/` | QUERY7准备38项完整离线PASS，工具提交前dirty保留 |
| `../inputs/ci-42450429814/` | 当前真机已验收b2b99e7内核 |
| `../logs/socdelay240-b2b99e7-live-20261108-1401/` | 历史体验PASS但阶段INCONCLUSIVE；USB断流及signal中断、动作/主机观察/重放/完整SHA结果 |
| `../logs/touchcontrol-b2b99e7-live-20261108-1347/` | 新核touch-only本轮PASS；420s完整控制采集及结果 |
| `../logs/soconly-ordered-60a2dc5-live-20261108-1235/` | 历史45s触摸FAIL、中央动作确认，当时ECM现场保留；全量capture/result/replay/receive-timeline |
| `../logs/touchordered-60a2dc5-live-20261108-0111/` | 先前touch-only本轮用户PASS；raw/capture/包时间/动作指令/result/replay，尾USB变化待确认 |
| `../logs/allprobes-da8ffc1-scmoff-live-20261107/` | 最近失败boot.log、fastboot.log、identity、result；历史用户触摸FAIL |
| `../logs/touch-theory-replay-20261108/replay.json` | 10份保存RAW的host原函数重放与顺序模型差异；不是新真机结果 |
| `../logs/touch-ordered-preflight-20261108/replay.json`、`../logs/gpu-offline-touch-ordered-20261108/` | 修正版10日志0差异及34 PASS/0 FAIL/0 SKIP完整离线回归 |
| `../inputs/ci-42391773972/`、`../logs/ci-42391773972/` | 历史60a2dc5有序触摸上报内核、SHA/commit、完整构建日志 |
| `../logs/ci-42380203110/`、`../inputs/ci-42380203110/` | 历史da8ffc1完整内核构建日志、artifact、commit及SHA |
| `../logs/scminfo-a0fa7a5-live-20261107-155024/` | 六次SMC完整in/out及查询后用户UI PASS |
| `../logs/touchdiag-7ed0427-live-20261107-115339/` | 旧19事件／大量空FIFO触摸失败对照 |
| `../logs/touch-power-control-20261107-121209/` | 旧触摸基线恢复那一轮，不是最新失败之后已恢复 |
| `../logs/{touchonly,soconly,socfuse,socfusearc}-*/` | 逐项触摸隔离轮；精确名字由logs目录及gpu-acceptance索引核对 |
| `../logs/gpu-offline-tz-startup-query7-final-20261107/receipt.json` | 历史33项完整离线PASS |
| `../logs/recovery-tz-acquisition-20261107-clean/` | 4MiB TZ原件、身份、挂载／recovery日志及采集receipt |
| `../logs/tz-native-ghidra-20261107/` | 私有Ghidra DB、逐轮证据、失败／成功headless日志、verifier输出 |
| `../logs/pas13-source-audit-20261107/report.json` | 固定原厂加载路径审查 |
| `../logs/pildxe-static-20261107/`、`../logs/scm-static-20261106/` | UEFI客户端实物证据，不是TZ运行态 |
| `../logs/android-pas13-preflight-20261107/result.json` | 拒绝现有Android取证boot的安全预检 |

| 关键材料 | SHA256 |
|---|---|
| 最近失败boot.log | `551770b8b8832c345c8ec2982893c461f71f785027e1a39203b57583496b60b3` |
| 最近失败result.json | `8439a3df64465a6876e0f065cb3bf56a5391794c0ca901f60b48e0a0e2a031fe` |
| QUERY7最终离线receipt | `9532352ee229bc536a1f479cc0231f6b1bc13176c364260f8f7d7dfdfddd7064` |
| TZ原件 | `989389bcf4527b2b8e445998aba49454e556ac554c18d80e9eb999a8cbbdb114` |
| TZ clean采集receipt | `59ebe18cbff96a2ba5fc05d492761cc53685f1022e6e5c3d4556971994451980` |
| evidence-startup-init.json | `f632cb9475879797b8247eda13393e606f5f40b8dd4854b1ba684c4cc315cdfc` |
| verification-startup-final.json | `f3c48c7b94d49d87d2acd14f8b7b808914e505de52dc4e14ce70d75de50c90cb` |
| 已保存FDT `/tmp/live-cepheus.dtb` | `bdd364a16ceabe9960e128103162e5280b150303624a123dfd71e871f9a43274` |

失败receipt的sample25初值false是匹配式错误，已改为true并解释：实际原日志
`CEPHEUS OSM: sample25`四组均存在。原boot.log未改；不把元数据纠正伪装成新测量。

专有GPU固件仅本地：`a640_gmu.bin` SHA `3ff0c02708bbe78641db887fa62f3a7f9337934d0c2ce0b961ef7c43172591d2`；
`a630_sqe.fw` SHA `a0e1b583f620fabe32729ce367959d1960638663244d7d0cfc21b9a5215a018b`；
`a640_zap.elf` SHA `74a948e84f0aac830b314ed05d26006b3571c9a3f90600c708ca76c25612660d`。
摘要与容器布局通过不验证ZAP签名／认证。

## 10. 操作约束和工具

- 只对核对过的67eea163/cepheus测试；首选临时`fastboot boot`，不刷boot。
- **不改persist、modemst1/2、fsg、fsc等校准分区**，不格式化现有iOS data。
- 不启动已被预检拒绝的Android boot；不把通用recovery称为整机只读。
- 触摸诊断默认关、严格无修复副作用；不自动reset／scan重发／电源试写／固件或校准更改。
- `cephscm=1`默认关、固定信息查询、一轮一次、fail-closed；不猜ID、不尝试auth
  探路、不加no_auth、不改PILDxe名单、不从失败推断旧ZAP已加载。
- 不开放用户态任意MMIO／PM4／电源投票接口，不绕过安全固件，不扫描secure活RAM。
- 不删除有效NC／PA检查，不用boolean覆盖“制造支持”，不伪造tools／测试成功。
- 新镜像另名、失败证据保留；每轮result绑定镜像／内核／日志SHA，逐镜像UI验收。
- 编辑用apply_patch，读取用read，GitHub用gh；不对脏树reset/clean，不复活USB控制功能。
- 子代理仅在用户授权范围内使用；此前授权是只读审查，不操作手机／派生子代理。

工具：现有 `cepheus-build:latest`／root-tools／apfs-tools；Ghidra在`/opt/ghidra`，
Capstone／LLVM／Rizin可离线核对，无IDA/BN/Hopper。换工具不等于改变证据等级。

USB日志：手机192.168.7.2，主机192.168.7.1/24，UDP5140，host MAC
`02:00:5e:77:ee:01`；`scripts/watch-usb-log.sh`只配置该MAC，需要用户sudo。
`scripts/udplog.py`用于接收，不新增USB控制端点。监听必须先于boot准备，
sudo密码不发到聊天；不要宽泛pkill导致无关任务被杀。

## 11. 可复跑的离线检查（不操作手机）

在 `/home/yangqi/cepheus-port/gpu-work`：

```bash
# 查询策略与实际callback仅编译／RAM验证，不执行SMC
python3 scripts/test-scm-hook.py
python3 scripts/test-scm-codegen.py
python3 scripts/test-scm-source.py /home/yangqi/android/kernel/xiaomi/sm8150
python3 scripts/test-tz-evidence.py

# 固定最新内核的本地核验
python3 scripts/verify-touch-kernel.py ../inputs/ci-42380203110/kernel.macho
python3 scripts/verify-pmap-cache-kernel.py ../inputs/ci-42380203110/kernel.macho

# 仅查询GitHub构建状态
gh run view 42380203110 --repo YangQi0408/UniXNU-cepheus
```

全量 `scripts/validate-gpu-offline.py` 的真实fixture／reference路径及命令记录在
最新receipt每个check中；另选不存在的output目录运行，不覆盖历史证据。
TZ verifier需显式原件／证据／新output；不在公共CI下载私有材料。
上述命令的成功不提供硬件执行授权。

## 12. 下一步／待办优先级

| 优先级 | 任务与依赖 | 验收标准 |
|---|---|---|
| 已完成本轮 | **#66：QUERY7固定支持查询** | 新基础控制及查询后触摸PASS；7次转录完整，command7可用而PAS13=false，不追加相同查询 |
| P1 | **#63：静态PAS13与真机false差异** | 不改现有事实，建立实际conduit／运行映像／参数／表变化的有界证据；无安全通道则保持UNKNOWN |
| P1 | **#33/#30/#58：GPU平台资源与加载后端** | 离线补齐ARC/clock/GDSC、DDR、SMMU及PAS buffer/加载契约；RAM测试不冒充真实资源所有权 |
| P2 | **#67／#60：触摸间歇根因** | 最新基础控制/查询后PASS，历史FAIL与240s阶段缺证据保留；继续离线调查，不试写恢复或重复整套触摸轮 |
| P2 | **#45：退休／停止** | 实际GX/CM3及DMA静默；不确定保留隔离资源 |
| P3 | **#15/#16：GMU→CP→图形前端** | 先真实fence，再绘制，再iOS接入，不能用一步成功代替全部 |
| 可选 | **#65：RAM-only取证镜像** | 全路径不挂持久FS／不自动写入审查，目前没有已验收候选 |

#62原厂路径审查、#64取得TZ原件已经完成，不再把它们写成材料缺失。
#63只完成了静态证据部分，原因调查未完成。历史#53标题“一次机会”不再适用。
用户后续要求直接下一阶段，并尽量省去测试。最新基础控制及查询轮均已结束，
本轮不追加手机测试或重跑完整离线套件。手机保持当前可用状态，不自动重启/
恢复；将工作转回离线GPU平台实现与PAS13差异。触摸间歇调查和GPU执行未完成，
不能因为固定查询完整而声称接近可运行GMU/CP/Metal。

## 13. 专题文档导航与阅读顺序

1. 本文 → [验收与纠错流水账](gpu-acceptance.md)（倒序最新在前）。
2. [触摸](touch-bringup.md) → [失败理论与重放](touch-failure-theory.md)
   → [QUERY7测试边界](scm-query7-preflight.md)。
3. [TZ原生证据](tz-native-evidence.md) → [PAS13源码审查](pas13-supported-audit.md)
   → [ZAP资源契约](zap-platform-contract.md)。
4. [CPU/GPU实现](cpu-gpu.md)、[性能](performance.md)、[根镜像历史](root-image.md)。
5. [参考项目](gpu-reference-projects.md)、[上游隔离同步](upstream-sync.md)、
   [RPMh固件证据](rpmh-firmware-evidence.md)、[SCM客户端](scm-static-evidence.md)、
   [PILDxe客户端](pildxe-static-evidence.md)。
6. [Android取证拒绝预检](android-pas13-preflight.md)、[panic回fastboot审查](panic-fastboot.md)、
   [原一次机会历史策略](gpu-one-shot.md)。

`README.md`、`../dist/README.md`及上述历史章节可能有“尚未／当前推荐／已可用”
等当时措辞；**以本文、最新gpu-acceptance和对应真机result为当前解释**。
不要照搬历史flash/recovery/失败镜像命令。本文是交接摘要，不声称全部缺口已解决。
