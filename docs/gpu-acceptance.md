# Adreno 640 实现路线与验收边界

## 当前结论

目标是Mi 9/SM8150上的iOS16.7.16图形加速。**尚不能证明“理论完全可行”，
更没有完成硬件加速。** 原因不是缺少状态机，而是仍有明确未实现的平台后端、
安全固件加载、共享内存同步和用户态ABI。本文把可实现的路径及其条件具体化，
不以空回调或ready布尔值代替这些条件。

用户已验收d336276镜像主屏幕、触摸、电源键正常；只读GPU探测确认CX实际off。
USB自动重启功能已取消，不恢复。用户离开电脑后不重启手机，不做电源试写；
最终统一验收时才逐阶段启用硬件实验。

## 证据与分层状态

| 层 | 现有产物 | 当前证据 | 下一道验收门槛 |
|---|---|---|---|
| 保底系统 | GPIO触摸/PON/软件显示 | 用户实测通过 | 每个新硬件阶段不得破坏 |
| GPU域观测 | gpu_power + soc probe | CX_HW=0x5ff4，blockers=0x7a/0x37a | 保留完整原始日志 |
| ARC资源发现 | arc parser + read-only probe | 解析负例/完整XNU构建通过 | 实测cx/mx/gfx表与继承槽 |
| 电源时钟 | gpu_power_txn | 每回调故障注入通过，只有mock | 当前DRV投票/资源归属、真实ACK、频率实测 |
| 内存映射 | gpu_vm + smmu_plan | 属性/越界/冲突离线通过 | allocator/缓存/TBU/CB/ASID归属，实际fault检查 |
| GMU启动 | gmu_firmware + gmu_loader | 本地固件7块解析、mock每阶段失败 | PDC/RSC序列、映射、CM3新鲜BABEFACE |
| HFI | ring/table/消息/会话/boot | 真实ring模拟固件握手、线格式对照通过 | A640版本/PERF/BW/功能/CORE_START真机协商 |
| GPU CP | PM4/fenced submit | 有界scratch包与超时mock通过 | GX/SQE/ZAP/non-secure、实际scratch/fence |
| 离屏绘制 | 尚无实现 | 无 | 有限格式clear/blit/三角形及hash对照 |
| 显示加速 | 现有SweetSurface/Framebuffer软件路径 | 源码与此前实测 | GPU buffer导入、fence同步、软件回退 |
| Metal/IOAccelerator | 尚无Adreno实现 | 无 | 用户态实现/ABI/着色器与资源同步完整接入 |

## 1. 电源/时钟后端不能省略的工作

- ARC表为u16 raw VLVL，数据字段投的是索引。GMU200MHz对应最低raw48。
  CX父电源依赖MX；提升先MX后CX，保留比最低需求更高的原有投票。
- `rpmh_read()`在Android通过TCS发read请求；返回到底表示该DRV投票还是
  聚合状态，不能仅凭函数名推断。需要设备/固件接口证据。
- 同一apps DRV的completed vote可以在TCS释放后留存；不允许“选空TCS→写48”
  覆盖其他用途。若无法恢复所有者模型，应停在诊断，不做降低或猜测性写入。
- 冷启动要求CX/GX都off、HLOS软件控制、GMU halted；GDSC配置REST2/FEW8/
  CLK_DIS_WAIT8后clear collapse，轮询GPUCC0x1540，不只看0x106c。
- GPLL0-div需真实300MHz及父时钟租约，RCG0x1124选择6/div1.5，UPDATE完成后
  才开分支。该RCG启用safe_config，按照clk_rcg2_enable在切换期间force-enable
  root、等待root on，再改配置/UPDATE并撤销强制位；新旧父时钟须已持有。
  复用当前代码的命名寄存器接口，不能开放任意MMIO。
- power_txn故障处理只是“保留而不降票”，不是热安全方案。实际部署还需要
  温度监测、有限会话和可证明的停止路径，否则不允许长期带电试验。

## 2. GMU/SMMU的条件

固件来自本地合法设备的a640_gmu.bin，不提交专有固件。SHA固定在cpu-gpu.md。
ITCM/DTCM经AHB上传；ICACHE/DCACHE及HFI/log通过GMU虚拟地址映射。TCM窗口由
GPU基址word offset换算，不能误把GMU物理基址再叠加GPU绝对offset。

GMU SID4/5带0x400 mask。SMR冲突检查只解决地址选择，不解决安全所有权；
必须核对ID寄存器、TBU连通、继承的CB/ACTLR、安全CB保留及ASID分配。
安装顺序：GMU停在reset→stream FAULT→表clean→配置CB→限定ASID/TLB同步→
寄存器核对→stream TRANS。超时不能全局清SMMU，也不能先释放表。
当前planner未执行这些操作。现有映射API只接受已知连续PA区，真实离散页表
还需要由受控IOMemoryDescriptor/IODMACommand分段枚举，而非kernel VA强转PA。

PDC/RSC微序列来自对应SM8150修订、实际cmd-db地址和供电依赖，不能直接把
Android裸常量搬入未验证环境。释放CM3 reset之前要清旧init结果、设置chipid、
HFI/log地址、AHB fence与power config，并重新确认固件/数据cache发布完成。
BABEFACE只表明CM3初始化，不表明HFI/硬件GPU可用。

HFI v2队列还必须管理固件版本、单一producer、超时后不重用seq、错误队列和
CM3 fault。A640的源码明确跳过legacy INIT；版本/PERF/BW/FEATURE/CORE_START
消息依赖真实频率、电压和互连表，不能用空表骗过握手。共享queue header包含双方字段，应使用coherent或匹配的uncached映射；
不能在有对方dirty数据的同一cache line上做粗暴invalidate。

## 3. CP/SQE/ZAP与最小GPU执行

Android `adreno_a6xx.c:a6xx_microcode_load`把SQE地址写入CP，然后按secure能力
通过PIL加载a640_zap；ring启动后调用adreno_set_unsecured_mode。
这一安全世界路径尚未在XNU移植/测试。**不要通过关闭安全检查、禁用保护寄存器
或未经证实的直接unsecure写法绕过它。** UEFI有SCM协议源码不等于ExitBootServices
后XNU可调用，也不等于已获得PIL/PAS内存与认证接口。

第一个命令仅使用内核自建scratch PM4，不接受应用原始命令：
CP_MEM_WRITE→WAIT_FOR_IDLE→CP_EVENT_WRITE/CACHE_FLUSH_TS完成戳。
验收必须同时看到期望scratch、独立fence、新鲜序列、无SMMU/CP错误，以及
bounded quiescence。fence可见但GPU未停稳时不可unmap/free。单次提交后不做
自动重试，故障后保留内存并报告具体阶段。

下一步才做固定RGBA/BGRA8离屏clear/blit、再到三角形；必须有CPU参考结果、
边界guard页、缓存一致性和正常/超时释放测试，再接显示。

## 4. iOS接入：先有限显示加速，再讨论Metal

本机源码`SweetSurface.h/.cpp`：surface持有IOMemoryDescriptor，普通surface为
IOBufferMemoryDescriptor，但不承诺物理连续。`SweetFramebuffer.cpp:present`
按层查surface，逐行CPU memcpy到ABL连续splash framebuffer。

较小的落地路径是在**实际GPU blit已通过**后为Framebuffer提供可选GPU blit：
持有源surface引用→固定权限映射→缓存发布→提交→fence→完成后展示/释放。
跨帧引用、CPU/用户映射修改、熄屏并发、超时与软件回退必须明确。绝不能GPU
仍在写时CPU同时执行fallback，也不能fence未完成就把surface释放/回收到其他进程。
该路径最多减少拷贝开销，不会使CoreAnimation的所有绘制或应用Metal自动加速。

完整Metal还缺：
- 适用于iOS16的MTLDevice发现和用户态驱动装载方式；仅注册名为IOAccelerator
  的服务无法证明Apple闭源驱动可用，不能向系统谎报硬件能力。
- resource/heap/texture/pipeline/command queue/fence等用户态对象与内核提交ABI；
  每进程内存隔离、命令验证、引用生命周期、错误上报和并发调度。
- Metal着色器输入格式到Adreno ISA的编译链与验证，pipeline/renderpass、
  tiling/GMEM、纹理格式等实现。Mesa/Turnip可参考，但依赖的Linux内核接口
  要替换；MoltenVK方向是Vulkan→Metal，不能创造缺失的Metal后端。

当前仅依据本机源码确认上述接缝，没有检查闭源用户态ABI、做替换驱动或声称
能无修改运行Apple AGX驱动。这是“完整可行性”仍未解决的独立研究/实现任务。

## 统一验收顺序与交付定义

1. 原系统回归（已经通过d336276），新ARC镜像仅补采表与投票证据。
2. 供电/时钟：实际readback/计数、正常停止、故障不降他人投票、温控。
3. GMU：新鲜固件启动结果、正确版本HFI、日志和错误队列。
4. SMMU/CP：专用scratch+独立fence，成功/错误/超时后内存无误释放。
5. 离屏图像逐像素对照，再进入可回退的Framebuffer blit。
6. Metal是独立后续里程碑；必须应用真实调用+渲染/同步测试才能宣称支持。

离线回归命令：`bash scripts/test-gpu-stack.sh`（目前28个程序、UBSan）。
补丁顺序：`python3 scripts/test-gpu-arc.py /path/to/pinned/ios-sweet/xnu/patches`。
mock日志标注“mock/no backend/not installed”，任何失败不得标完成。

## 本次离线交付记录

代码提交：58a944c修复ARC staging；dc8dab6电源事务；b51f525映射/提交契约。
CI41396451358（b51f525）通过14组C/UBSan、patch顺序测试及完整XNU构建。
产出内核SHA256：
`4fee603741f3a38874a749528a31c2af065e086061922ee5f2953e3885d0f7d4`。

只读ARC候选镜像：`boot-cepheus-gpu-arc-b51f525.img`，SHA256：
`0ba44c063c4e25d1316eac5a5ff39c1e3b5e4b37a4f809a8036253944f8d0da3`。
仅在已有无网络构建容器打包，保留原ADT/UEFI/DTB及触摸电源设置。
**尚未启动该候选；用户不在场期间没有设备操作。** 电源事务/SMMU/提交模块
仍为host-only测试，不在公共内核中执行；内核新接入只有ARC只读诊断。

## 后续续作：HFI端到端与具体固件写入

- `cepheus_hfi_messages.h`输出固定59-DWORD PERF表、160-DWORD BW表、17-DWORD
  ACD表及FW_VER/FEATURE/CORE_START消息。频率Hz转kHz；ARC索引根据供应的真实
  表选择，缺MX依赖/错序OPP/坏BCM地址/越界wait mask拒绝，未使用的尾部清零。
- `test-hfi-source-layout.py /path/to/kgsl_hfi.h`读取本机Android结构定义到临时
  编译fixture，实际sizeof/offsetof和完整字节比较通过，避免测试仅重复自己的
  线格式假设。未把Android专有设备输入或固件加入仓库。
- `cepheus_hfi_session.h`直接调用共享ring write/read，等待正确req_hdr ACK；
  一次只允许一个未完成请求，4095序列用尽后不回绕，100ms/10000次双上限。
  错误/不匹配/未知异步请求标记session与queue不可重试。ACK外层ID不强求等于
  命令ID（KGSL匹配依据是回显req_hdr），避免虚构ABI要求。
- `cepheus_hfi_boot.h`按A640顺序进行FW_VER→PERF→BW→ECP disable→可选ACD
  配置→按已验证bin选择LM→CORE_START；要求调用者给出有效bin策略和对应
  throttle mask。模拟固件从真实ring取命令再回ACK，覆盖每条命令失败、队列
  wrap/full、陈旧ACK、版本错误、异步错误、时钟倒退/冻结与超时。
- `cepheus_gmu_upload.h`具体执行ITCM/DTCM的LE32 stores和ICACHE/DCACHE映射
  拷贝，而非write_block成功桩。先验证全部目标、别名、计划/固件重叠和映射
  范围，再在独占reset lease内写入并完成cache/MMIO发布。对本地a640_gmu.bin
  实际7块内容在RAM模拟TCM/缓存窗口复制后逐字节比对通过。没有映射真机MMIO。

上述仍需平台提供已验证的固件摘要/不可变缓冲、真实映射、reset/power租约和
cache屏障后才能连到硬件；模拟成功不代替实际GMU启动。命令输入由内核构建，
不作为应用任意PM4或任意电源投票接口。

## XNU集成续作（54e8951构建通过，未真机执行）

`cepheus_smmu_install.h`现已实现限定context安装顺序：验证预留资源→发布全部
表→配置CBA2R/CBAR/TCR/TTBR/MAIR/ACTLR→限定ASID失效与有界TLBSYNC→先SCTLR，
再SMR，最后S2CR由FAULT转TRANS。任何实际写入后失败均隔离，绝不全局reset。
ACTLR不是随意填0，begin后端必须验证该平台策略；当前测试使用假寄存器。

`cepheus_gpu_compile.c`把HFI boot、实际上传、SMMU install、电源事务和提交
函数纳入真实XNU C编译/链接，后续CI核对所有源码以及符号。没有启动调用，
不会因这些符号存在就执行供电或DMA。

`cepheus_gpu_memory.cpp`接入真实IOKit内存API，并提供默认关闭的`cephmem=1`
CPU-only自检：分配20KiB、显式prepare、MapperNone读取host PA并验证连续性，
写入/读取pattern后complete和release。不使用kernel VA强转PA。40bit PA mask
与4KiB对齐，单次<=256KiB、总预算512KiB。选择WriteCombine/Normal-NC，而非
InhibitCache/Device DRAM；后者可能触发XNU managed-page保护或错误访问语义。
核对了pmap的缓存属性更新路径，但该自检尚未真机执行，不能证明缓存行为。
已发布给设备的buffer没有bool式“done”释放捷径，仍需真实停止/解除映射实现。

CI41456898966首次链接失败因0087缺少尾部patch上下文，被git按EOF语义拒绝；
54e8951加入SweetBattery后文并在实际XNU配置副本apply验证。
CI41458264813完整通过18组C/UBSan、集成/patch测试与六入口链接检查，下载
内核SHA256 `757d6d5fa85650fddb94e9ddb0596ec6a9039639978934d518c481ecf63b5690`。
这证明真实XNU API/编译链接可用，不证明设备内存属性、DMA、供电或提交已验收。
RpmhDxe进一步证据与限制见[rpmh-firmware-evidence.md](rpmh-firmware-evidence.md)。

## 实际FDT配置，不沿用通用v1参数

`scripts/gpu-fdt-config.py`直接解析保存的DTB（不需要运行dtc），解析边界、节点、
phandle、实际OPP引用、speed-bin表、BW路径、GMU/SMMU和ACD配置。记录输入SHA，
默认不选择speed bin；有eFuse/内核证据后才能显式选，不会默认使用最快档。

本机live FDT SHA `bdd364a16ceabe9960e128103162e5280b150303624a123dfd71e871f9a43274`
解析结果：GPU引用`/soc/gpu_opp_table_v2`而不是`/soc/gpu-opp-table`；
非零GPU OPP为257/345/427/499.2/585/675/770MHz，raw VLVL为64/128/192/224/
256/320/320。speed-bin0/1各自throttle mask为0x80/0x40，当前实际bin未知。
GMU仍为200MHz。PDC config/seq是`0x0b290000/0x0b490000`（各64KiB），
而非通用sm8150.dtsi的v1地址。SMMU ACTLR条目`0,0x407,0x303`匹配SID4/5及
mask0x400，因此GMU context的期望ACTLR为0x303，不应沿用测试中的0x10。
源头分析依据`arm_smmu_fwspec_match_smr/qsmmuv500_init_cb`，并非真机寄存器读数。

验证命令：`CEPHEUS_LIVE_FDT=/path/live.dtb python3 scripts/test-gpu-fdt.py`。
这只导出配置；BCM的cmd-db unit/width/VCD和当前电源投票仍需要相应证据。

### CM3启动与BCM续作

1d81a7b实现具名GMU寄存器启动序列：仅chipid0x06040001、无重叠HFI/log区间，
在已持有power/reset/SMMU/PDC/image前提下清旧init、配置启动地址/chipid/fence/
IFPC、解除CM3 reset，等待新BABEFACE及HFI status，再校验HFI v2。每次操作
前校验租约与fault、双时间/次数上限，失败隔离不重启。CI41461702642完整通过，
仅证明七入口编译/链接及19组host测试，不代表已在真机解除reset。

FDT脚本进一步解析真实有向互连路径：DDR为GPU→LLCC→MC→EBI，BCM依次SH0/
MC0/ACV；CNOC为GPU→GEMNOC-SNOC→GEMNOC→CNOC，BCM为SH2/SN15/SN3。
`cepheus_bcm.h`依据KGSL实现unit/width/VCD元数据、AB/IB归一化、稳定VCD排序、
每组末项commit和对应command-index wait位（不照抄上游按usecase索引位的疑点）。
值饱和到14bit，零流量仍可携带commit但valid=0。真实cmd-db元数据只读探测新增
上述六资源，不写票；ACV是RSC相关bitmask资源，不能套普通BW归一化。还没有
声称这些输入足以生成最终HFI BW表：需完成ACV身份/聚合策略及实际cmd-db采样。

### 有界scratch ring与SQE装载格式

`cepheus_gpu_ring.h`按KGSL32KiB ring实现空间预留、尾部NOP wrap和publish→WPTR
顺序。它只接受内核生成的10 DWORD scratch+fence包，不接受任意用户PM4；
检查scratch与ring VA不重叠、容量、read/write指针及已有inflight状态。发布失败
之后不回滚写指针或释放内存。成功只表示入队，仍须fence+停稳验证，当前是一发
测试路径，未提供清除inflight来绕过回收门槛。

`cepheus_sqe.h`对应`_load_firmware/adreno_get_ucode_version`处理SQE第一DWORD
剥离与版本编码；实际a630_sqe.fw（原记录SHA未变）解析为version0x16ee190、
8075 DWORD、32300字节payload，RAM拷贝逐字节校验通过。没有启用SQE或绕过ZAP。

### XNU内部内存池

内存模块扩展为16槽/1MiB总预算的真实IOKit池：64-bit句柄含generation和
slot，旧句柄不能释放重用后的分配；借出的CPU view阻止publish/free；CPU-only
buffer可以配对complete/release；publish之后没有取消发布/直接释放接口。
分配与complete操作在mutex外执行，槽位保持保留；完成失败保留原descriptor
与预算供未来恢复，不遗失到临时栈变量。generation用尽则退休槽位而不回绕。
API仅为内核内部用途，未建立IOUserClient或允许用户控制物理地址。
单独生命周期测试覆盖lease/代次/发布/不确定完成路径，真实IOKit调用仍需完整
XNU构建与后续cephmem自检；不把CPU pattern成功等同于DMA一致性验证。

### 单GPU客户端带宽表生成

`cepheus_gpu_bw.h`现可从明确路由节点宽度/ports、每个BCM的unit/VCD及单客户端
AB/IB构建HFI BW输入；同一BCM覆盖多个节点时取最大归一化值，按VCD稳定排序，
所有usecase的地址顺序/wait保持一致。FDT已确认路径BCM指向apps RSC(cell-id8000)，
故ACV只使用该RSC定义的bit3（值8），不是普通IB数值，也不假装它是GMU自己的
硬件DRV投票。该表来源对应KGSL查询apps interconnect route的路径。
仍缺真实cmd-db unit/VCD采样以及平台端聚合/所有权接入；只对合成元数据执行，
没有发电源/带宽请求。多客户端聚合、特殊ALC/其他RSC不在此API支持范围内。

### 固件身份与故障快照

XNU GMU upload和SQE copy入口现在先用corecrypto SHA256验证固定本地artifact
大小/摘要（host用OpenSSL/CommonCrypto）。两个真实文件通过，单bit破坏拒绝。
这是文件身份校验，不是Qualcomm PAS签名认证；输入仍须在hash至使用期间保持
不可变，不能把可被用户修改的共享buffer直接传入。

故障快照接口依据后端当前可安全访问的域分别读CP/GMU/SMMU；没有GX访问租约
就不读GX，未读域不能记成clean。保留CP fault/protect、SMMU FSR/FSYNR/FAR及
时间戳，FSR前后改变或MULTI时不声称FAR原子一致。不做IRQ W1C/clear/reset，
避免破坏第一现场或误清其他客户端错误。该接口仍是具名寄存器后端，不对应用开放。

固件生命周期进一步使用真实XNU私有快照：copy之后对私有内存计算固定digest，
sealed状态不再允许获取可写view，也不允许直接publish为GPU DMA目标；只读lease
在实际GMU上传/SQE复制整个期间阻止free。内部调用者必须传入可读固定的内核源，
不是裸用户指针；seal是可信内核API约束，不声称能防恶意内核代码直接改指针。
快照不执行文件系统读取、不自动加载专有固件、不暴露用户态方法。

`test-gpu-memory-host.sh`把**同一份生产C++文件**与隔离的IOKit测试替身编译，
不是重复实现分配器；验证8线程/800次分配与view释放、预算满额、失败prepare、
短物理段、旧句柄、封存后禁写/持读禁free、真实固件私有拷贝及complete失败保留。
物理地址和IOKit操作是测试替身，不作为真机证据。arm64使用真实dsb屏障；
非arm主机屏障仅在显式HOST_TEST且非KERNEL时可编译，不能进入XNU构建。

内存池按实际`page_size`计费：SWEET为16KiB，GPU表仍为4KiB。20KiB HFI请求
实际分配32KiB；4KiB请求实际占16KiB，均计入1MiB预算。CPU view返回实际
分配容量，GPU只映射明确需要的4KiB区段。host测试刻意模拟16KiB主机页，
并验证槽位用尽、页取整和固件valid-length不随allocation-size混淆。

GMU VA计划连接allocator与页表：ICACHE@0x4000、DCACHE@0x44000各240KiB；
HFI@0x60000000映射20KiB，log@0x60100000映射4KiB。host按16KiB取整的多余
页面不进入GPU VA；目标PA必须来自明确owned backing、不能重叠。全部Normal-NC，
ICACHE privileged RO/execute、DCACHE/log privileged RW/NX、HFI unprivileged RW/NX。
初版512KiB预算不足以同时容纳480KiB缓存、表/HFI/log/固件快照，已提升到有界1MiB，
保留单次256KiB限制及完整预算测试；不是无限增长的分配器。

`cepheus_gmu_workspace.c`已将真实分配接口→独占CPU views→GMU VA计划→五页三级
页表→HFI队列表连接起来。工作区持有5个分配（ICACHE/DCACHE/HFI/log/tables），
任何中间失败按逆序释放CPU资源；complete失败保留句柄和descriptor，不丢弃
恢复信息。`cephmem=2`是默认关闭的CPU-only整体工作区自检，不写SMMU、不发
电源请求、不发布GPU DMA。host测试实际调用该C文件和真实内存池C++实现，
覆盖准备成功、每个槽位不足位置、物理段不完整、部分清理失败及重入拒绝。

### PDC/RSC冷启动脚本

`cepheus_gpu_pdc.h`固定A640-v2的48项配置/微序列写入，word-offset转换为每个
窗口的byte offset，要求实际PDC地址为0xb290000/0xb490000。三个ePCB资源地址
0x30010/0x30000/0x30090按KGSL及Linux v6.12同源序列保留，但不虚构其名称；
begin后端必须将其与本次cmd-db、所需索引含义、GPU域归属和停止状态核对。
冷启动拒绝已启用的PDC/TCS1/TCS3；先写完整payload，最后才enable banks/PDC。
测试遍历48写入和全部回调失败，失败后不关共享电源、不释放潜在设备资源。
该模块没有apps-DRV投票操作，也不能解决apps投票继承；当前只在假寄存器执行。

### A640v2 bin策略修正

`adreno_a6xx.c:a6xx_efuse_power_features`对speed_bin0清除LM控制位，
`kgsl_gmu.c:gmu_aop_mailbox_init`对A640v2/bin0直接返回，不启用ACD。
因此HFI boot不再统一发送LM enable：bin0为8档（含off）、throttle0x80、
无ACD/LM；bin1为7档、throttle0x40，需要ACD且启用LM。策略解码仅接受
chipid0x06040001和明确QFPROM读数(mask0xe0000000>>29)得到的0/1，其他拒绝。
FDT仅提供可选bin，不能凭表存在推断本机是0或1；当前尚未读本机speed-bin。
两种握手命令序列均有测试。独立TSENS/运行时限等热安全仍是平台要求，不能
把原驱动关闭某硬件特性的bin策略误解为无需温控。
