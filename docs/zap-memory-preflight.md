# ZAP内存预检：真机执行记录与离线准备

## 2026-11-08 18:16：六条执行成功记录，严格日志未通过，UI验收正常

用户回复“已进入”后核对67eea163/cepheus及候选SHA1f986169...，采集READY后
18:02:56发起一次临时boot，发送/Booting均成功，未flash。18:05:13收到begin、
两条translated、pattern PASS、MDT restored及end result=0/retained_pages=0。
这是主机接收时间，不是精确内核运行时间。MDT PA0x184398000/16384B，4个
4KiB页attr=0；镜像PA0x185d00000/1048576B，256页attr=0x44、1MiB对齐，
logical4096B。两块范围不重叠，MDT恢复attr=0xff。六条执行记录及几何核验
通过，未补造任何记录；本轮未执行PAS/SMC/GPU。

最早的scheduled delay_s=90没有收到，因此原七条严格核验结果保持NOT_PASS。
不能声称完整预检验收PASS或证实90秒时序。日志链路首包在采集+63.270秒，
但缺失原因不能仅凭此确定；不为这个缺项自动追加测试。用户随后回复“正常”，
确认主屏幕、触摸和熄屏唤醒后触摸PASS，主机观察18:16:04。回复在采集结束后，
不能把该操作追认成已同步收到的触摸事件。执行事实与UI验收可用于后续推进，
不再安排相同内存预检或控制轮；七条完整日志的NOT_PASS记录保留。
预检后9条health，末条events=3/errors=0，不能用错误为0替代人工触摸验收。

采集18:07:46自然到期300.000947秒，340包/91661B；SHA256
479b2f821c4a1a7d56aa455e646ee6224cf228d243d497db9ab460efe308948e。
主机网卡RX errors/dropped均0，watcher未记录断开；UDP仍无完整送达保证。
capture exit0，自己的专用MAC watcher已正常请求停止，容器退出143符合SIGTERM。
未自动重启/恢复/重试，手机保持候选运行，用户已确认当前交互正常。
证据：[本轮结果](../../logs/zapmem-b5006b5-live-20261108-1802/result.json)、
[严格判定](../../logs/zapmem-b5006b5-live-20261108-1802/zap-strict.json)、
[执行事实](../../logs/zapmem-b5006b5-live-20261108-1802/execution-observations.json)。

## 离线准备记录

2026-11-08 17:55 +08:00离线实现、完整XNU构建与镜像打包核验均完成，尚未真机执行。
目标是验证新增的真实IOKit强序映射、CPU翻译属性、1MiB对齐连续分配及清理。
此前普通小块Normal-NC自检不能替代它；host IOKit/PAR替身也不能证明真实页表。
用户要求继续直到需要真机，且尽量减少人工测试，因此只准备一次合并预检。

## 已实现

- ZAP MDT使用kIOMapRealTimeCache/VM_WIMG_RT；SWEET构建拒绝HAS_UCNORMAL_MEM
  打开及非0x17编码。与原厂MDT一致的目标类型为Device-nGnRnE。
- 在映射前核对pmap管理范围、对齐、真实PA和完整连续长度；映射后每个主机页
  查WIMG，每4KiB用AT S1E1W/PAR核对物理地址和有效内存类型，全部通过才写入。
  ZAP新映射及恢复映射均显式kIOMapPrefault：AT不能触发缺页，不能用“先读写
  一下”替代验证前建页表。prepare提供UPL，IOKit按既有prefault路径填充映射。
  指令按XNU mmu_kvtop模式保存/恢复DAIF，屏蔽IRQ/FIQ，ISB后读PAR，不改页表。
- Device元数据只用volatile字节存取，AArch64编译核对只有STRB写入，没有
  memcpy/bzero或向量写。1MiB镜像保留Normal-NC，逻辑长度仍为4096B。
- 释放MDT前显式恢复Copyback，并核对pmap属性、新旧两个映射的CPU翻译结果。
  恢复失败、原别名未更新或IOKit complete失败均保留backing/预算，不重试。
- 生产快照工作区与预检共用同一分配/验证/清理路径。预检不需要固件，工作区
  ALLOCATING期间拒绝访问和第二次分配；不会给出可发布GPU/PAS的handle。

## 单次流程

仅显式cephzapmem=1；不含cephsoc/cephsocdelay/cephscm/cephmem/boost/fuse/ARC/GX。
任何冲突参数即使=0也拒绝，普通启动不分配新内存。线程等待90秒后分配
16KiB MDT和1MiB镜像，验证映射，填充/回读有界pattern，清零并清理，然后退出。
全程CPU所有；不扫描既有物理地址、不secure RAM、不SMC、不给GPU地址或DMA。

日志必须按次序完整包含scheduled、begin、两条translated、pattern PASS、
MDT restored及end result=0/retained_pages=0。analyze-zap-memory-log.py核对
顺序、PA/属性/尺寸、无重叠和对应MDT恢复；丢包、重复、失败、混入其他探测
均不记PASS。只有末尾PASS字样不够；UDP断线时保存现场，不自动重启或补发。

此轮只需要在预检完成后一次简短点按/滑动和熄屏唤醒反馈，不重复完整触摸
清单或已完成控制轮。实际操作前核对67eea163/cepheus，采集先启动，仅临时boot。
本离线阶段不进入fastboot、不启动镜像、不更改目前已验收系统。

下一轮执行时先启动专用USB日志watcher和capture-usb-log.py，确认监听READY后
再临时boot候选。采集上限300秒；收到完整预检结果并完成一次简短UI确认后
即可停止自己启动的采集，不为凑满时长继续等待。保存boot.log、packets.jsonl、
capture.json，使用analyze-zap-memory-log.py生成独立结果；不自动重启或恢复。
同核touch-only镜像仅供离线严格差异核验，不安排新的控制轮。

## 构建记录

生产实现为99c6dba（包含prefault修正）；构建提交b5006b5仅再修复CI的KDK
下载。CI42467907788完整成功，下载后核对76核心入口、18内存API、无unresolved、
staged source、触摸ordered及pmap 64-bit PA均PASS。内核17415576B，SHA256：
f1b5a674a6ff858f50511f5c6d11a9b126701e6e04bd2a4427f0cc40063a72d1。
内核见[CI产物](../../inputs/ci-42467907788/kernel.macho)，完整记录见
[build-verification.json](../../logs/zap-cache-20261108/build-verification.json)。

| 产物 | 字节数 | SHA256 |
| --- | ---: | --- |
| [候选boot-cepheus-zapmem-b5006b5.img](../../dist/boot-cepheus-zapmem-b5006b5.img) | 70680576 | 1f986169db84c5a7eec8cd1b208f70811a676aa2bb26a8a5e071b510d663c95d |
| [仅离线比对用touchcontrol-b5006b5](../../dist/boot-cepheus-touchcontrol-b5006b5.img) | 70680576 | 504f1636fb84d8ce6f8953e0e723c371eb1c2a24529bb7c9a43eb44116e46ea7 |

打包使用现有cepheus-build容器，禁网、普通UID、无设备映射。新控制版相对
已验收b2b99e7控制镜像的payload仅kernel.macho变化，TOML及其他文件字节
不变；候选相对新控制版保持同核，仅增加cephzapmem=1，其他TOML字段、
ADT、固件、DTB及boot头均核验。两份*.verified.json位于dist，额外的
control-config-verification.json及receipt.json位于上述证据目录。
离线阶段两个新镜像均未boot；随后只对候选执行一次临时boot，结果见本文开头。
同核控制镜像仍仅用于离线比对，不追加控制轮。

前序CI42465594915被旧GX测试的源码提取边界阻断，fde990f已修复并通过。
CI42465792206及42467439486均在KDK安装时报image not recognized，尚未进入
XNU编译，二者为失败而非取消。本机随后复现浏览器下载URL返回HTTP504的
HTML错误页；release API可读到二进制，SHA256SUM与manifest均给出
29866842eef039812888daea4176ce493be153b0d6d1b88f90753d69ea51b7d4。
CI现用gh release download、有限重试、固定SHA256及hdiutil verify后才安装
相同13.5/22G74 KDK，不改变内核源码或编译目标。失败日志均保留在证据目录。

## 范围

PASS最多说明本次CPU内存映射/读写/恢复正常，不能证明secure PA可达、XPU保护
粒度、签名认证、PAS加载或GPU执行。PAS13=false仍未解释，加载门禁不变。
crypto带宽、认证/停止协议及GPU电源/SMMU所有权还需实现。下一项依赖真机的是
这些新内存映射在实际XNU/SM8150上的行为，不用PAS认证来探测映射是否成立。

离线证据目录：../logs/zap-cache-20261108/。11种无固件预检故障场景及5种真实
ZAP工作区场景在ASan/UBSan通过；共享GPU内存回归使用真实GMU/SQE/ZAP通过。
9项staging、生产线程门禁、参数封装、AArch64字节访问及日志判定测试通过，
修改C++生产单元静态分析无告警。ASan保留故意隔离场景，关闭leak检测，非无
泄漏声明。prefault修正后再次通过针对性host场景与修改单元静态分析，见
zap-host-prefault.log及static-analysis-prefault.log。未重复完整
validate-gpu-offline入口；完整CI及镜像核验结果见上，不替代真机证据。
