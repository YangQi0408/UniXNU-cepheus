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
