# ZAP内存预检：下一次真机测试点

2026-11-08离线实现完成，完整XNU构建与镜像打包进行中，尚未真机执行。
目标是验证新增的真实IOKit强序映射、CPU翻译属性、1MiB对齐连续分配及清理。
此前普通小块Normal-NC自检不能替代它；host IOKit/PAR替身也不能证明真实页表。
用户要求继续直到需要真机，且尽量减少人工测试，因此只准备一次合并预检。

## 已实现

- ZAP MDT使用kIOMapRealTimeCache/VM_WIMG_RT；SWEET构建拒绝HAS_UCNORMAL_MEM
  打开及非0x17编码。与原厂MDT一致的目标类型为Device-nGnRnE。
- 在映射前核对pmap管理范围、对齐、真实PA和完整连续长度；映射后每个主机页
  查WIMG，每4KiB用AT S1E1W/PAR核对物理地址和有效内存类型，全部通过才写入。
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

## 范围

PASS最多说明本次CPU内存映射/读写/恢复正常，不能证明secure PA可达、XPU保护
粒度、签名认证、PAS加载或GPU执行。PAS13=false仍未解释，加载门禁不变。
crypto带宽、认证/停止协议及GPU电源/SMMU所有权还需实现。下一项依赖真机的是
这些新内存映射在实际XNU/SM8150上的行为，不用PAS认证来探测映射是否成立。

离线证据目录：../logs/zap-cache-20261108/。11种无固件预检故障场景及5种真实
ZAP工作区场景在ASan/UBSan通过；共享GPU内存回归使用真实GMU/SQE/ZAP通过。
9项staging、生产线程门禁、参数封装、AArch64字节访问及日志判定测试通过，
修改C++生产单元静态分析无告警。ASan保留故意隔离场景，关闭leak检测，非无
泄漏声明。未重复完整validate-gpu-offline入口；完整CI结果另行补录。
