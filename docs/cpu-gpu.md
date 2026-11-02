# CPU 和 Adreno 640 加速工作

**完整GPU/Metal加速尚未实现。** 当前可用基线为GPIO触摸＋软件合成主屏幕＋电源键。
GPU初始化、测试提交、绘制、iOS图形集成分别验收，不能用识别或编译通过代替成品。

## OSM 实测（run 41291850807）

只读探测boot SHA256 `5c4f2312f419a4552b2a4dcdfe81b69152e7ac79f747c54de24f6f8fc40745d4`。
`soc-boot.log`：

| 域 | 地址 | PERF_STATE_DESIRED | LUT目标 | 周期计数约值 |
|---|---|---|---|---|
| L3 | 0x18321000 | 13 | 1536 MHz |1536 MHz|
| Silver | 0x18323000 |15|1632 MHz|1633 MHz|
| Gold |0x18325800|0|710.4 MHz|711 MHz|
| Prime |0x18327800|0|825.6 MHz|827 MHz|

每簇CORE_DCVS_CTRL=0，因此只用core0计数代表共享时钟域；其他槽计数0不是CPU离线。
采样时间由独立架构计时器换算，短采样含读取/调度开销；LUT目标不等于实时精确频率。
当前软件没有动态调频和异构性能调度集成，Gold/Prime持续低频有直接硬件证据。

下一版 `cephsoc=1 cephboost=1` 仅做120秒Gold/Prime实验：扫描固件LUT，在不超过
1.4016GHz且open-loop电压mV与当前相同的候选中选择最大非single-core档位。
Gold为index6、Prime为index5，mV字段均756；virtual corner会变化，由OSM硬件处理。
不写电压/PLL，不关闭LMH限制，不改L3/Silver，不宣称相同mV就等于完全相同功耗。
结束后恢复旧目标；若其他组件改了目标则不覆盖其请求。尚缺持续温度管理，
因此这不是生产级governor或永久强制高频方案。运行中出现高温/不稳定需停止测试。

## GPU 状态与边界

live FDT：chipid0x06040001，GPU0x02c00000，GMU0x02c6a000，GPUCC0x02c90000，
GPU SMMU0x02ca0000。寄存器定义来自本机Android `drivers/gpu/msm/a6xx_reg.h`，
其中GMU是相对GPU的**word offset**，不能当GMU基址下的byte offset。

真机GPUCC：CX_GDSCR=GX_GDSCR=0x222001；GMU_CLK=0x80004220，GPUCC_AHB=
0x80000001。未满足CX上电/GMU时钟条件，探测没有触碰GMU/GX或启动GPU。
GCC CFG_AHB=1可访问GPUCC，不等于图形核心已供电。

本机Android提供的固件（仅记录摘要，不上传专有固件）：

| 文件 | SHA256 |
|---|---|
|a640_gmu.bin|3ff0c02708bbe78641db887fa62f3a7f9337934d0c2ce0b961ef7c43172591d2|
|a630_sqe.fw|a0e1b583f620fabe32729ce367959d1960638663244d7d0cfc21b9a5215a018b|
|a640_zap.elf|74a948e84f0aac830b314ed05d26006b3571c9a3f90600c708ca76c25612660d|

已开始PM4最小测试缓冲构建（`cepheus_a640_pm4.h`）：CP_MEM_WRITE到显式拥有的
GPU scratch范围＋WAIT_FOR_IDLE；离线校验奇偶位、64位地址、边界、容量。
它不执行GPU工作，不创建GPU VA，不能把任意CPU物理地址传入就提交。

仍需按依赖顺序实现：
1. CX/GX电源、时钟/RPMh、GMU启动及HFI协商；遵守隔离/复位顺序。
2. GMU与GPU SMMU地址域、缓冲所有权/缓存一致性、SQE/ZAP加载路径。
3. 受限ring提交、IRQ/轮询完成、fence、超时恢复；先验证scratch写入。
4. 离屏绘制，再接IOSurface、同步和显示合成。
5. iOS Metal/IOAccelerator用户态接口、着色器支持和CoreAnimation接入。

Freedreno/Turnip可参考Adreno命令、编译器和资源布局，但现有Linux/Android接口不能
直接作为iOS驱动使用。MoltenVK是Vulkan→Metal，不会提供缺失的Adreno Metal后端。

## CPU限时实验结果（run 41292760384）

boot SHA256 `da9efd9cb358959e813052f4ab35afdda76d334611699050c6dd1e2501a0df3b`，
kernel SHA256 `916140ce0965f4078e46c05832f85d74198573b34ea1e83d8f0250a6da97d26d`。
已临时启动，没有写boot/userdata。保留触摸和快速熄屏，日志 `cpu-trial-boot.log`：

```
trial gold old=0/710400 new=6/1401600 mv=756 old_corner=0 new_corner=6 duration=120s
trial prime old=0/825600 new=5/1401600 mv=756 old_corner=0 new_corner=5 duration=120s
sample1 gold core0 desired=6 target_khz=1401600 ... rate_khz=1402452
sample1 prime core0 desired=5 target_khz=1401600 ... rate_khz=1402562
restored gold desired=0
restored prime desired=0
sample25 gold ... target_khz=710400 ... rate_khz=710851
sample25 prime ... target_khz=825600 ... rate_khz=826318
```

频率请求生效和回退都有直接证据。CPU频率提升不是界面帧率提升的等价指标；
本次没有足够持续交互样本，不能声称流畅度翻倍。尚无温度闭环/调度整合，
不把这个限时实验包装成长期性能成品。

GPU基础新增 `cepheus_gmu_firmware.h`：依据源码格式规划GMU分块，严格检查
长度、对齐、地址域、重叠和溢出，零长度元数据不当成载入数据。当前测试使用
合成数据，并未通过该模块上传真实固件；仍缺电源、GMU/HFI、SMMU和SQE初始化。
PM4与固件规划模块均不发MMIO、不启动GPU，不构成Metal支持。

## GPU续作：供电证据与诊断修正

USB受控重启/诊断功能已由用户取消；回退提交`1b59abd`的代码树与`35561c0`
完全相同。GPU在独立`feat/adreno640`分支继续，不携带USB控制代码。原有
CDC-ECM日志保留。GPU分支单独选用macos-15/Xcode16.2以避免退役runner。

新的`cepheus_gpu_power.h`及SoC探测补齐以下**只读**检查：

- live FDT与Android `sm8150-gdsc.dtsi`指定CX的`hw-ctrl-addr`为
  GPUCC+`0x1540`。`gdsc.c:poll_gdsc_status`实际轮询该寄存器bit31；
  旧探测只看`0x106c`的GDSCR不足以判定CX实际状态。
- GDSCR的collapse位是HLOS投票，不代表拥有该域；不能覆盖TZ/HYP等投票。
- GMU寄存器只在CX实际on、CFG/AHB/GMU/CXO分支active及RCG非更新/非off时
  才允许本诊断读取。`0xffffffff`一律拒绝，HWCG位单独置位不授权访问。
- 同时记录DDR_AXI、MEMNOC、SNOC_DVM时钟阻塞位，但全部正常仍不代表SMMU
  映射、缓存一致性或DMA所有权已经建立。仍不读取GX或SMMU，不写电源寄存器。

这里发现的实际供电依赖尚未完成：

1. live FDT的CX父电源是`VDD_CX_LEVEL`/`cx.lvl`，GX父电源是PM8150L S2。
   现有触摸用RPMh VRM enable-only接口不能直接当作GPU ARC corner投票接口。
2. CX Linux初始化配置EN_REST=2、EN_FEW=8、CLK_DIS_WAIT=8；快照`0x222001`
   不能证明已执行这些配置。启用前要检查HW_CONTROL，不能夺取硬件控制权。
3. CX状态timeout为500us；GMU启动先开CX，再将GMU clock设为200MHz，依次
   开GMU/CXO/DDR_AXI/MEMNOC/AHB。200MHz来自GPLL0-div与RCG分频，不应为这一步
   随意改写GPU PLL。关停还需尊重共享电源投票和设备树skip-disable语义。
4. GPU SMMU为`qcom,skip-init`；GMU user/kernel SID为4/5、mask为0x400，
   不能清空全局SMMU或破坏继承的安全上下文。

参考源位于本机Android树：`drivers/clk/msm/gdsc.c`、
`drivers/clk/qcom/gpucc-sm8150.c`、`drivers/gpu/msm/kgsl_gmu.c`、
`arch/arm64/boot/dts/qcom/{sm8150-gdsc,sm8150-gpu,msm-arm-smmu-sm8150}.dtsi`。

整理现有草稿为离线模块：GMU阶段式loader（每一步失败清理与不确定资源隔离）、
HFI v2 ring和三层32-bit VA/40-bit PA页表。HFI补上payload发布完成后才更新
write index的顺序；发布或doorbell不确定时将queue标为不可重试，防止重复命令。
实际HFI头部含双方字段，仍需coherent映射或证明cache-line所有权安全的后端。
这些模块目前没有真实供电/SMMU后端，不会启动固件或执行GPU命令。
