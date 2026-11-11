# A640 ZAP/PAS 平台接入事实与缺口

## 2026-11-12：原生 crypto BCM 传输已补，真实资源租约仍缺

`cepheus_zap_bcm_raise()`复用GPU BCM的actual RPMh sender/shared lock/poison/
idle reservation，仅接受固定CE0/SN11/SN0/SH0/MC0/ACV。动态address/unit/
width/VCD仍须由有真实owner保护的command DB取得；不使用固件结构偏移作
MMIO地址，不把AB/IB直接当encoded X/Y。两个入口分别拒绝对方专用ID，
最多6条命令，不因ID总数扩到9放宽。crypto/GPU各1977 owner失效边界与29
uncertain stores、cross-route/ARC隔离、ASanUBSan/native/staged检查PASS。
本批只是受限投票传输能力，不会聚合完整client状态或生成ACK ledger，
没有真实owner/provider/startup/PAS caller/crypto clocks/secure PA授权。
local receipt out/crypto-bcm-local-20261112-v2/receipt.json SHA
8f9ce1f35f41408418f08640666d17ee94790d404f06c6acd368ba9a26072e8e；
固定固件路由static复验PASS，详见bcm-active-backend.md。完整XNU链接待验，
旧accepted核/映射镜像不含此production change，不复用旧receipt。无phone/
实际投票/SMC/认证/GPU执行；PAS13=false/CP halted、secure PA cache及真实
crypto时钟/资源所有权/认证阻塞不变。

## 2026-11-11：owner provider 边界只提供接线合同

新增 `cepheus_resource_owner_provider.h`，为未来的真实 owner 后端提供
fail-closed 接线：provider 自己持有 lease 和证据，必须在采集前后验证同一
lease；只接受显式 EXCLUSIVE_REAL 且非模拟模式，并精确绑定 provider/context、
boot/owner/lease/resource/ACK epoch、RPMh 与 crypto ledger。所有证据复制，
输出不得覆盖输入、provider context 或 gate。每次 boot 只允许一次尝试；任何
已尝试失败都以同一 context 调用 retain，并永久保留 uncertain 状态。错误
pre-attempt 合同或已有非零 gate version 会在尝试前拒绝。

这不是 owner 后端，也没有默认实现、启动调用者、MMIO/SMC/PAS、crypto vote、
clock 或 GPU 操作。focused host fixture 只验证拒绝和保留行为，不是真实
owner/ACK。PAS13=false、supported=false、CP halt 均不变。

## 2026-11-11：secure resource evidence 合同已接入，仍未取得资源所有权

新增 `cepheus_zap_resource_evidence.h`。它把 PAS 准入所需的 secure PA/XPU/
cache、crypto route/BCM/ledger/ACK 与冻结 owner evidence 绑定成一份可复制、
自校验的记录。`cepheus_zap_pas_start` 在 expose 前要求该记录通过 owner gate、
PA 对齐/不重叠、4K 或 16K XPU granule、CPU/secure cache 属性、crypto master
23/slave 0、六项 BCM、IB/AB、ledger/ACK 和年龄窗口；自封记录、混源、
REFUSED/UNCERTAIN owner、输出 alias 和过期证据均失败关闭。

PAS transport epoch 与 owner `resource_epoch` 是两个独立域。transport lease
继续使用 PAS epoch，resource evidence 必须绑定冻结 owner 的 resource epoch；
不得要求两者数值相等。可能 expose 后的失败仍永久 KEEP，expose 前失败走
CPU_ABORT。foreign/stale/uncertain/simulated 证据进入永久 uncertainty retention。

这条合同只证明“记录满足格式和交叉绑定要求”，不证明记录内容来自真实
owner、XPU、cache 或 crypto 后端，也不产生 BCM vote、ACK、TBU ownership、
PAS 认证或 GPU 执行权限。`PAS13=false`、`supported=false`、CP halt 保持不变。
真实 owner、继承 ACK/ledger、双 TBU、secure PA/crypto 后端与认证 PAS 仍未解决。

## 2026-11-11：authenticated allocator binding，仍无运行态准入

`ceph_zap_memory_authenticated` is an allocator-record check, not an owner or
PAS proof. It succeeds only after the fixed lifecycle reaches AUTH, with the
same handle/claim, released source snapshot, no CPU views, both buffers
published, and exact MDT/image PA, capacity and layout identity. A forged or
stale claim, copied VA alias or changed capacity is refused. It returns no
caller-facing VA and cannot authorize CP unhalt, PAS mutation or GPU admission.

CP output-safety checks now cover the separate ZAP workspace. While ZAP is
ALLOCATING or COPYING, they fail closed without reading fields mutated outside
the pool mutex. Once stable, MDT/image backing aliases are refused together
with the ordinary GPU pool and metadata records. ZAP keeps handle index16,
separate accounting and an irreversible KEEP path on uncertain completion.

Actual firmware validation passes 24 PAS transport modes, five memory modes
and ten original-SQE CP build-pool modes under ASan/UBSan with no report. The
receipt is `out/zap-live-boundary-receipt-20261111-v1/receipt.json`, SHA256
`9825d5bce9ffb63ed2b7896bf3f26eab268e60122dbf6a029c378ec02749811a`.
This uses host IOKit doubles; no phone, SMC/PAS call, CP unhalt or GPU code ran.
Real owner/inherited ledger, TBU0/1, secure PA/crypto, runtime PAS permission
and ACK remain independent unresolved gates. `supported=false` still refuses.

## 2026-11-10：PAS13 内存条件收窄，未取得运行态准入

本机TZ静态PAS13 flags+20c=0/+20d=1、callback+228=0：mem_setup跳过一般
load-region登记，认证前默认路径不做权限转换而检查已有页访问mask；HLOS
mask8是一个subset候选。不能无条件要求固定carveout或另加hyp_assign，
也不能反向当本次XNU pair权限已通过。25段原字节/634指令/17分支核验普通
及`-O`通过，receipt相同。运行TZ/flags/权限/DDR表与MDT reader/cache仍未知，
资源acquire/lease/quiesce及PAS13=false仍缺，门禁不变。详见
[PAS13内存合同](tz-pas-memory-contract.md)。无手机/SMC/内核变更或新测试点。

## 2026-11-08 20:25：原生PAS生命周期已完整构建，资源后端仍缺

新增 `cepheus_gpu_zap_load/stop/status` 私有内核入口。与现有INFO查询共用同一
原生SMC回调及互斥锁；只使用本次启动的实际查询缓存，拒绝未查询、错误、
availability不全或supported=false。没有新增boot参数或启动调用，不会自动
重复查询，手机继续保持已验收zapmem-b5006b5，未执行PAS mutation或GPU。

`cepheus_zap_pas.h` 直接调用生产IOKit工作区的私有claim/expose/reload/finish，
按固定PAS13 init1 → mem_setup2 → 重新复制镜像 → auth5顺序执行，分别检查
transport和secure返回；16次调用限制中断续调，不承诺单次SMC返回时间。
32-bit ABI拒绝高PA，不截断；mem_setup仍传4096逻辑长度，不传1MiB容量。
共享前锁住两个buffer及不可变源快照；普通get/release拒绝访问。mem_setup
后的复制只写image，不再触碰已交付的MDT。成功认证后仍保留MDT/image；只有
同epoch真实GPU停止/排空及PAS6成功后才尝试IOKit释放。失败、BUSY、中断预算
耗尽或租约失效均保留内存和共享资源，不能自动重试、释放或声称认证成功。

这些是真实分配器及原生SMC接线，**不是已完成的PAS平台后端**。资源acquire/
lease/quiesce/release仍无实现：须取得实际crypto带宽/时钟、PAS13/GPU epoch
所有权，验证该pair的secure PA/XPU/cache合同，以及停止GPU的实际协议。
接口不提供默认成功实现；现有CPU预检不能充当这些证明。真机PAS13=false
未解释，因此不会调用init/auth探测或解除CP门禁。

针对性验证：实际a640_zap文件+生产工作区，RAM secure transport下24种场景
ASan/UBSan PASS（源文件持有、并发CPU拒绝、mem_setup后镜像重载、MDT不改写、
双层错误、中断续调、租约失效、真实停止前禁止释放及complete失败保留）；
既有CPU工作区5种相关回归、image边界/别名、9项staging、查询缓存/一次性门禁、
整个native PAS段AArch64编译及两个修改生产单元静态分析通过。下游调用顺序和
ABI核对针对固定36f369f；没有重跑完整离线入口。证据
`../logs/zap-pas-backend-20261108/receipt.json`。该receipt的6项源码哈希已核对
属于93093d3，保留其当时的构建PENDING字段；a2c14ba仅修复INFO/PAS固定日志
literal及相应codegen断言。CI42476845818 @a2c14ba已完整构建成功，内核
17449096B，SHA256
af9729b1f8b3df8f13915d6e1a1f6bd79bfc1e60d6e6845ddbafe87611faa65e，位于
../inputs/ci-42476845818/kernel.macho。81核心入口+22内存API已链接、无
unresolved，staged source、INFO/PAS日志标记、触摸有序上报及PA64 cache
callback核验PASS；后续证据为同目录build-verification.json、run.json和
build-followup.json。未打包/boot，GPU/Metal未执行，资源后端与启动hook仍缺。


> 最新状态见[工作交接](current-work-handoff.md)及[TZ证据](tz-native-evidence.md)：
> 本机TZ原件已经取得，静态表确有PAS13；与真机supported=false仍未解释。
> 2026-11-08新基础控制与query7-basic均完成；command7 availability=0/1，
> PAS13 supported=0/0，七次记录完整，查询后触摸/唤醒PASS。认证未尝试，GPU未运行。
> 下文“缺TZ／未发SMC”部分是历史状态，不放宽认证／加载／CP门禁。

完整结果及日志SHA见[QUERY7实测](scm-query7-preflight.md)。本轮排除command7
不可用，但false原因与认证能力仍UNKNOWN。后续离线补齐平台资源与加载后端，
不重复相同查询或控制轮，不将可用性查询当作认证证据。

18:05后续ZAP CPU预检已收到真实MDT Device属性、镜像Normal-NC属性、读写及
Copyback恢复成功记录，end result0/retained_pages0。缺初始scheduled标记，
严格七条日志NOT_PASS；用户已回复“正常”确认主屏幕/触摸/唤醒。它补充CPU内存执行事实，
不改变secure PA/XPU未知、PAS13=false或加载门禁，详见
[ZAP真机记录](zap-memory-preflight.md)。

## 2026-11-08：独立ZAP内存工作区，仍为CPU准备

后续推进：MDT现已切换到Device-nGnRnE候选映射，并接入实际CPU翻译检查、
byte-only复制、释放前Copyback恢复与失败保留；新增独立单次CPU内存预检，
默认关闭。不再使用前一版MDT的Normal-NC实现。下一真机点、离线证据及限制
见[zap-memory-preflight](zap-memory-preflight.md)。99c6dba再补齐验证前prefault，
b5006b5仅修复CI的KDK下载，CI42467907788完整通过。候选zapmem-b5006b5已
打包、严格核验为同核控制仅加cephzapmem=1，尚未boot，停在一次CPU预检前。
这仍未建立secure PA/XPU合同或认证能力。下面为1b34034阶段历史。

新增cepheus_zap_memory_prepare/get/put/release_cpu，接入生产IOKit分配及已封存
ZAP快照。工作区独占一个MDT/image pair，独立预算1MiB+16KiB；GPU池仍保持
单块256KiB/总1MiB。镜像要求1MiB连续和PA对齐，MDT按主机页取整（SWEET
16KiB）；保留6860B MDT、4096B逻辑长度和1MiB实际容量，不把容量传作逻辑长度。
分配mask显式编码对齐，不放宽PA上界；PA范围、连续性、对齐在cache重映射前
检查，随后逐主机页验证Normal-NC。完整容量先清零，复制后只允许只读lease。

工作区handle与普通GPU池分开，不能交给GPU publish/view/release；源快照持有到
两次分配和复制结束，之后可独立释放。分配中拒绝第二个prepare，lease阻止释放，
generation拒绝旧handle，GPU terminal与分配竞态会清理尚未暴露的pair。局部失败
释放可确认仍为CPU所有的部分；IOKit complete失败则保留该部分的backing/预算，
整个工作区永久隔离、不重试、不允许新pair覆盖。没有调用方done=true释放捷径。

这补齐了实际分配/封存/CPU清理路径，**不是PAS-compatible allocator或加载
生命周期**。Normal-NC与下游MDT的STRONGLY_ORDERED并未证明等价，secure PA可达、
XPU粒度/别名和共享资源仍未知；新API明确禁止把PA送给PAS。没有硬件publish、
认证/停止入口或启动调用。后续必须建立secure访问和cache合同，再按原厂
init_image/mem_setup/load/auth顺序接入；不能把预复制完成当作认证或zap_loaded。

本机5种真实固件工作区场景在ASan/UBSan下通过，包括分配/映射/prepare/cache/
对齐失败、部分清理失败、8线程lease、旧handle、独立预算和terminal竞态；
MDT/b02及全容量零填充比对通过。共享分配器有改动，因此运行了一次相关GPU
内存host套件（GMU/SQE/ZAP真实材料），9项integration、PA mask测试、修改C++
单元静态分析均通过。首次旧套件误传a630_gmu.bin被固定身份检查拒绝，改用
a640_gmu.bin后通过，原始失败日志保留。ASan关闭故意隔离场景的leak检测，
不声称无泄漏。没有重跑完整离线入口、没有手机操作；证据
../logs/zap-memory-20261108/。CI42459697884 @1b34034完整XNU构建成功，下载后
核对76核心入口/17内存API、无unresolved、staged source、触摸及PA64均PASS。
内核17431888B，SHA256
4ef4961a4e53c421b52feeb52ae8f87ac3de28a0ee64d17263e942028271edc5，保存在
../inputs/ci-42459697884/kernel.macho；build-verification.json记录核验结果。
没有打包或boot，手机仍运行已验收b2b99e7查询版。

同轮继续核对缓存类型：固定SM8150的arch/arm64/mm/dma-mapping.c:48及
arch/arm64/include/asm/pgtable.h:395将STRONGLY_ORDERED映为Device-nGnRnE，
WRITE_COMBINE则为Normal-NC，二者源码类型不同。本机XNU参考树的
IOMemoryDescriptor.cpp:362将kIOInhibitCache映为VM_WIMG_IO，但pmap.c:5652
对DRAM选择POSTED_COMBINED_REORDERED，proc_reg.h:1106及start.s:767对应
Device-GRE；因此不能仅换成kIOMapInhibitCache就宣称复现原厂MDT类型。
kIOMapRealTimeCache/VM_WIMG_RT在HAS_UCNORMAL_MEM关闭的源码分支会选择
Device-nGnRnE，可作为下一步审查候选；仍需核对实际SWEET构建配置/页表、
CPU别名转换和Device复制访问的宽度/对齐，再考虑secure PA/XPU与生命周期。
这些是源码证据，不是手机实际页表或PAS接受证据；没有更改当前Normal-NC。
源文件SHA与定位保存在../logs/zap-memory-20261108/cache-contract.json。

## 2026-11-08：内核ZAP元数据与重定位镜像准备

153561e新增cepheus_zap_image.h及cepheus_gpu_zap_snapshot_prepare，接入已有
固件快照池。CE_FW_A640_ZAP只接受14256B及固定ELF SHA74a948e8...，私有副本
通过digest核验后封存，持有只读引用直到MDT和镜像复制完成；错误kind、陈旧
handle、损坏文件拒绝。普通可写view和publish仍不允许用于封存固件。

C路径解析有界ELF32 little-endian/DSP6头、hash段和可重定位PT_LOAD，逐项
验证文件/地址溢出、重叠、入口、对齐与容量后才写输出。生成MDT为原始ELF头/
phdr加hash段；镜像按最低paddr重定位，BSS与间隙清零。输出缓冲与源、彼此、
layout结构重叠均拒绝；失败不留下部分复制。该解析器供受限A640容器使用，
不宣称通用ELF loader；生产入口另要求固定SHA，不信任调用方提供的plan。

本机结果：MDT6860B与现有.mdt逐字节一致，加载段1968B与.b02一致，逻辑镜像
4096B。allocation_bytes=1048576单独记录为后续PAS分配需求，不拿它替换逻辑
mem_setup长度，也没有扩大现有GPU池上限或分配PAS区域。生成的缓冲是CPU
暂存材料，未提供secure PA/cache/XPU保证，不能直接送给PAS或标记zap_loaded。

现有cepheus_gpu_memory.cpp单次最多256KiB、池总额1MiB，且按16KiB主机页
映射普通Normal-NC内存。新的14KiB固件快照可以使用它，但1MiB PAS驻留区
需要独立分配/预算与共享状态，不能只放宽该池上限；MDT的secure可访问性、
XPU粒度、PA范围和失败后隔离/释放证明仍须由PAS后端提供。

依据为固定SM8150 36f369fd的peripheral-loader.c中segment_is_loadable、
pil_setup_region、pil_alloc_region及已有本机split/MDT字节证据。ASan/UBSan
覆盖新解析/复制及生产C/C++快照路径，本机真实固件比较PASS；既有GMU/SQE
相关内存生命周期同次测试通过。9项integration和两个修改生产单元的Clang
静态分析通过；没有重跑完整离线入口，没有真机操作。证据保存在
../logs/zap-staging-20261108/。CI42457537126 @153561e已完整成功，下载后核对
SHA/源码commit及76核心入口、13内存API均链接，无unresolved；staged source、
触摸有序上报及PA64保留。kernel17414848B，SHA256
763d274e7220e86e610bc0b57474f894699b87018bd737f5831c4b90ae816341，位于
../inputs/ci-42457537126/kernel.macho。完整构建日志在../logs/ci-42457537126/，
本轮build-verification.json保存核验结果；尚未打包/boot，也不为CPU准备安排真机轮。

下一缺口仍为PAS可访问连续内存、crypto带宽、真实认证/停止生命周期，以及
GPU电源和SMMU平台所有权。本次没有解释PAS13=false，不新增SMC或改变加载门禁。

## 2026-11-07：PILDxe客户端实物证据不解除PAS门禁

已有本地日志未找到本机ZAP成功记录，指定目录也未取得本机TZ原件。
REA对既有PILDxe原件确认retail名单为UEFI本地策略、PAS1/2/5加载ops及
SMC64客户端helper；不是secure实现或PAS13认证记录，supported0原因
仍UNKNOWN。加载门禁/CP halt不变，无新SMC或手机操作。
详见`pildxe-static-evidence.md`与gpu-acceptance.md最新边界。

## 2026-11-07：原厂加载入口纠正（门禁不变）

固定SM8150 KGSL→SSR→PIL-TZ路径不以前置supported7作加载条件；
通用qcom_scm_pas_supported不是该GPU入口。已测supported7=0仍为false，
但“PAS13必然无法加载/认证”不能由此证明。真实认证能力及返回0原因
UNKNOWN，**本项目加载仍fail-closed，不改查询/CP halt门禁**。
PAS init/auth的crypto带宽租约是独立资源缺口，非已证实的supported0根因。
本轮只读源码/保存证据、无手机操作或新SMC；详见
`pas13-supported-audit.md`及gpu-acceptance.md最新纠正。
下方旧记录所称“阻断”仅为本项目当前策略，不是原厂GPU强制查询条件。

## 2026-11-07：查询后UI确认正常，仍不允许PAS加载

用户确认查询后系统仍正常，固定支持查询已完成且不破坏本轮交互。
PAS13-supported0不变，未执行init/mem/auth/stop，也未启动GPU。
后续先审查原厂调用和平台上下文，不能将“查询可返回”当“认证可行”。

## 2026-11-07：首次真实查询：64-bit可用，但PAS13-supported返回false

scminfo-a0fa7a5临时boot，原生6次SMC返回：64-bit约定self0/1，
INFO查询PAS1/2/5/6 availability均0/1，PAS13 supported7返回x0=0/
x1=0。当前conduit按原厂bool解释为false；不是认证结果，也不是所有
平台环境永不支持的定论。不得由availability0xf绕过supported0，
不猜其他ID、不试PAS init/auth作为探测、保持CP halt和GPU门禁。
本次**没有执行PAS mutation/ZAP加载或认证**。完整镜像/日志/寄存器
转录见gpu-acceptance.md和对应result.json。查询后真实触摸仍送达，
UI反馈待确认；下一步先审查原厂支持检查/平台环境，不能直接上电。
下方“未执行SMC”是此前历史状态，现已完成固定只读查询。

## 2026-11-07：查询候选接入显式开关，仍未SMC/PAS实测

用户要求继续受限支持查询。`cephscm=1`现默认关闭，主soc线程的
基线有界采样后才调用one-shot固定INFO/PAS13-supported引擎；出现
boost/内存参数即拒绝，不是任意SMC接口。只查询availability及supported，
无PAS1/2/5/6 mutation、共享buffer、认证/复位或GPU启动。最多16次
不保证单次secure call返回；每次寄存器日志只做取证、不自动恢复。
30项完整离线检查通过，XNU新构建/查询关闭基线/同核查询真机结果
仍待。旧2026-11-06“未hook”是历史状态，本次是显式opt-in候选，
不能据此把ZAP实例视为认证或GPU资源视为已拥有。详细记录见
gpu-acceptance.md。本轮未重启/启动手机或执行任何SMC。

范围：本地SM8150内核`36f369fdb77319a2984a48b89f025ac3e5f97386`、保存本机FDT
SHA256 `bdd364a16ceabe9960e128103162e5280b150303624a123dfd71e871f9a43274`，
以及已验证容器结构的本地`a640_zap`文件。**没有执行SMC、认证、加载或启动ZAP。**

## 已核对的链路

- FDT `/soc/qcom,kgsl-hyp`兼容`qcom,pil-tz-generic`，firmware-name `a640_zap`，
  PAS ID13；无`qcom,pil-no-auth`、无指定memory-region/subsys-vmid/clock-names。
  `scripts/gpu-fdt-config.py`现在解析并断言这些字段。缺少属性不等于不需要平台资源。
- `adreno_a6xx.c:a6xx_microcode_load`设置SQE地址后通过`subsystem_get(zap_name)`
  加载ZAP，失败不准继续。`a6xx_rb_start`随后才释放SQE halt、发CP init，并通过
  已认证的ZAP路径切入非安全执行。没有ZAP时直接改TRUST_CONTROL的参考回退
  **不是本移植允许的方案**。
- `peripheral-loader.c:pil_boot`顺序：解析MDT/分配映射 → proxy vote → init_image
  → mem_setup →（有VMID才做对应内存归属）→ load segments → auth_and_reset。
  不得先auth成功假设，再补内存；不得把完成回调假定为安全状态。
- `subsys-pil-tz.c:pil_init_image_trusted`复制元数据到物理连续DMA buffer；该下游
  使用`DMA_ATTR_STRONGLY_ORDERED`。`drivers/firmware/qcom_scm.c`同类实现明确写明
  4K对齐、连续、不可缓存以避免XPU违规。**普通GMU NC池不能自动当作PAS allocator**：
  还需SCM可访问PA范围、XPU粒度、缓存/CPU别名与内存归属的真实证据。
- init_image后原厂释放元数据buffer，是原厂已完成调用的行为；XNU在SMC传输状态
  不确定/超时的情况下不得照抄释放。auth失败同样不能盲目释放固件区域。

## 原厂调用参数（用于实现审查，不是可直接执行的SMC指令）

`include/soc/qcom/scm.h`：SVC_PIL=2，SIP FNID宏为`0x02000000 | svc<<8 | cmd`。
这不是最终SMCCC调用约定；不能跳过is_scm_armv8/SMC32或64发现。

| 阶段 | command | args | arginfo | 必须检查 |
|---|---:|---|---:|---|
| init_image | 1 | PAS13、MDT物理地址 | `SCM_ARGS(2,VAL,RW)=0x82` | 传输返回＋secure result |
| mem_setup | 2 | PAS13、region_start、logical size | `SCM_ARGS(3)=3` | 两层返回 |
| auth_and_reset | 5 | PAS13 | `SCM_ARGS(1)=1` | 两层返回，不等于CP已工作 |
| shutdown | 6 | PAS13 | `SCM_ARGS(1)=1` | 真实停止后才考虑归还内存 |

下游scm_call2读取`desc.ret[0]`；另一SCM64实现读取`res.a1`。传输层成功但secure
result非零不能当作认证成功。SCM PAS路径还会投票crypto带宽；FDT没列ZAP专属
clock/regulator不表示可以忽略该共享路径。不能复用GPU BW表冒充crypto路由。

## 本地文件与分配边界

`inspect-zap-layout.py`证据：ELF32/DSP6，14256B，三个phdr；MDT=148B头+6712B
hash segment=6860B。实际load段1968B，重定位逻辑mem_setup跨度4KiB，原厂按
段alignment分配1MiB。**mem_setup长度是逻辑4KiB，不是分配1MiB**，释放/accounting
仍需要真实1MiB区域。容器digest/布局验证不验证签名、DSP指令或secure策略。

## XNU当前必须补齐而未补齐

1. 固定SCM查询transport/convention已实测，ZAP CPU材料准备已实现；PAS mutation
   transport、共享buffer cache语义和失败/中断生命周期仍未接入。
2. PAS/crypto带宽和时钟资源的共享所有者；不能越过现有ARC投票继承问题。
3. PAS专用连续且对齐内存/MDT、真实PA可访问性、XPU保护粒度和可靠归还路径。
4. 固件生命周期绑定（已认证ZAP实例、GPU上下文、复位epoch一致）、shutdown证据。
5. GX/CP执行与非安全切换的正式协议，不能发no_auth或直接清TRUST_CONTROL。

因此当前继续停在离线材料/源代码可证明范围，不增加伪成功PAS后端、不在手机上
试SMC、不把纯planner或public CI通过视为认证可行性已确认。

## 2026-11-06补充：真实传输候选不是可信加载完成

`cepheus_scm_discovery.h`现在有受限的call-availability/PAS13-supported编码与
one-shot引擎；`cepheus_gpu_compile.c`有线程上下文、mutex、完整x0..x7输入输出
的原生SMC callback。`test-scm-codegen.py`提取**该生产函数本体**编译为AArch64
汇编/对象（不执行），源码测试核对info6/pil2/支持查询7及64->32约定。

只读查询后端**没有接入startup**，更没有提供PAS init/mem_setup/auth_reset/
shutdown或MDT共享内存。若将来人工批准使用它，成功也只能表示查询可用；
不能把supported=1作为ZAP实例已认证，不能给`zap_loaded`填true。
任何SMC单次调用都可能不返回，本地最多16次的引擎不提供secure-world watchdog。

本机UEFI的SCM二进制静态证据见`scm-static-evidence.md`。PSCI CPU_ON实测成功
说明PSCI conduit存在，不代表SIP PAS通道/共享crypto带宽/XPU区域也已验证。
