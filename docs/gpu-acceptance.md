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

离线回归命令：`bash scripts/test-gpu-stack.sh`（目前18个程序、UBSan）。
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
  配置→LM enable→CORE_START；要求调用者给出有效thermal throttle mask，不
  悄悄跳过温控。模拟固件从真实ring取命令再回ACK，覆盖每条命令失败、队列
  wrap/full、陈旧ACK、版本错误、异步错误、时钟倒退/冻结与超时。
- `cepheus_gmu_upload.h`具体执行ITCM/DTCM的LE32 stores和ICACHE/DCACHE映射
  拷贝，而非write_block成功桩。先验证全部目标、别名、计划/固件重叠和映射
  范围，再在独占reset lease内写入并完成cache/MMIO发布。对本地a640_gmu.bin
  实际7块内容在RAM模拟TCM/缓存窗口复制后逐字节比对通过。没有映射真机MMIO。

上述仍需平台提供已验证的固件摘要/不可变缓冲、真实映射、reset/power租约和
cache屏障后才能连到硬件；模拟成功不代替实际GMU启动。命令输入由内核构建，
不作为应用任意PM4或任意电源投票接口。

## XNU集成续作（构建待验证）

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
