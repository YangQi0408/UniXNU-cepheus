# cepheus ST-FTS 触摸 bring-up

## 2026-11-08：只做理论，确认批内接触转换丢失

用户已拿走手机，先做离线理论。10份保存RAW以生产FIFO/report原函数
host重放；最新失败19事件只发2组down/up，顺序模型5组，丢失的3组
enter→leave在同一批内。旧失败5组全发仍失败，所以这是真实上报缺陷，
**不是后段空FIFO根因已定位**。正常/失败可见初始化及SYSINFO一致，
原厂scan路径也不等echo，force-cal状态只记录日志。新增8项host测试；
生产驱动/镜像未改，无构建/CI/手机命令。细节见
[理论与重放](touch-failure-theory.md)，触摸验收仍FAIL/根因UNKNOWN。

## 2026-11-07：最新新核SCM关闭基线触摸FAIL，尚未恢复

用户明确报告`allprobes-da8ffc1`触摸坏。初期19条非空事件之后至16000次
轮询events仍19，I2C errors0；五个enter原始x均1079。日志中force-cal
状态在正常旧轮也见，不能证明根因。SCM关闭且未执行查询，不归因于
新QUERY7。旧触摸源码／参数相同也不排除启动时序／继承状态差异。
查询开启镜像没有启动，停止GPU测试；未自动复位／重发scan／改校准。
当前没有新的恢复验收，下面多轮正常属于历史记录，不是长期稳定保证。
完整镜像／日志索引见[当前交接](current-work-handoff.md)和gpu-acceptance。

## 2026-11-07：完整只读探测版本轮UI通过

用户确认allprobes-008b2cd正常，实际FIFO/HID捕获也有中央及多指
事件。当前版SHA8e538843...留作回退，不重启、不刷写。GX只触发
“GMU not powered/clocked; GX not read”拒绝分支，未验证GX寄存器
读取或GPU执行。逐项同核对照本轮通过，旧间歇性触摸失败根因仍
未知，不能宣称所有启动稳定或GX偏移修正解决触摸。详细矩阵及
镜像/日志身份见gpu-acceptance.md。

## 2026-11-07：GX修正新内核的GX关闭基线正常

用户确认socfusearc-008b2cd触摸正常；System Info只读返回身份字段，
不将cx0xffff/尺寸字段/CRC0当健康或损坏结论。随后补抓无数据，检查
时设备已在fastboot，非新的触摸失败证据。下一轮同新核仅加GX门控
只读探测，尚未启动；本次用户验收不能扩展为GX/GPU加速通过。

## 2026-11-07：ARC本轮正常；最后GX对照因离线缺陷暂停

用户确认socfusearc-7ed0427正常；补抓中央和finger0..3真实触点、
down/up各20，回退保留，当前手机不重启。旧GX探测有DWORD/byte
偏移及CP_HW_FAULT/GMEM打印错误，已修源码；此前失败日志门控跳过
GX读取，不能当触摸根因。重构内核后先GX关闭基线再单增GX，不能
同时把新kernel/SysInfo变量与GX混入。详细证据见gpu-acceptance.md。

## 2026-11-07：基础探测+fuse本轮触摸UI正常

用户确认socfuse-7ed0427正常，FIFO/HID和UI本轮均通过；仍未建立跨
启动因果。下一轮只恢复ARC只读快照/config dry-run，保持同内核，
GX/boost/内存关，不发电源票，不新增System Info读。当前可用回退
保留、不立即重启，详细证据见gpu-acceptance.md。

## 2026-11-07：仅恢复cephsoc本轮UI验收正常

用户确认soconly-7ed0427点击/滑动正常，该轮真实FIFO/HID证据见
gpu-acceptance.md。OSM/门控GPUCC/温度采样与触摸本轮可共存，不能
视cephsoc本身为已证根因；下一轮仅加fuse只读bin，ARC/GX等仍关。
当前可用版本保留、不立即重启。总体回归根因尚未定位。

## 2026-11-07：同一新版内核touch-only触摸正常

touchonly-7ed0427与失败touchdiag-7ed0427内核及其他payload相同，仅移除
cephsoc/arc/fuse/gx四个参数。用户确认「正常了」，追加日志有中央坐标
真实down/up，当前新版可用回退保留。探测组合/时序是重点但未确定单项
根因，不把跨启动状态当常量。下一轮先只恢复cephsoc主开关（包含门控
GPU状态/OSM/温度采样，不是纯CPU），仍无boost/内存/arc/fuse/gx。
证据与摘要见gpu-acceptance.md；当前手机留在可用状态。

## 2026-11-07：旧touch-power回退恢复，最新内核回归仍待定位

用户确认历史touch-power完整镜像c8eae5a0...本轮触摸恢复；启动前应该仅
重启，不是确认关机。追加只读日志有中央坐标连续touch0 down/up和touch1
事件，与用户操作一起证明输入恢复。当前可用回退保留，不立即重启。
不能说冷启动修复、continuation修复或新内核已稳定，后续touch-only参数
对照分离kernel与cephsoc/arc/fuse/gx探测。证据身份见gpu-acceptance.md。

## 2026-11-07：RAW版同样未恢复；扫描与控制器身份继续核查

用户已确认touchdiag-7ed0427失败。本次RAW可见真实边缘坐标和force-cal
状态01/30，但无530218，I2C无错误，随后大量空FIFO。continuation修正
不是本次已证明的修复；目前不宣称触摸可用。原厂System Info只读路径
readSysInfo(0)已编码为诊断候选A6/0000一次208B读，不发load-data、
校准/初始化、flash或reset。header不符仅UNKNOWN，不自动修复版本。
固件/config/CX/AFE/CRC字段/尺寸/通道数用于下一轮定位，不等于健康或认证。
新候选未真机验证，详细范围见gpu-acceptance.md。

## 2026-11-07：当前启动触摸回归，历史成功不代表现状

用户确认workspace-6f93116无法触摸（电源正常）；随后同内核memcheck-6f93116
对照也未恢复。两版内存CPU自检成功但UI触摸验收失败，暂停后续GPU硬件
阶段。两次日志有一组多指右边缘触点与释放；没有原始FIFO，原因未知。
不能仅凭active scan0/ID36:39/HID注册判触摸可用，也不能凭workspace模式
归因。旧基线镜像保留，未覆盖或刷写，不自动触发触摸reset/固件更新。

源码核对发现原厂fts_521事件循环在53:02:18用户报告后跳过下一slot，
并用static pre_id跨FIFO批次保留。本实现此前遗漏该协议。现在补齐单slot
continuation抑制及跨批次测试；这是真实源码修正，尚未证明与本次失败
有因果关系。I2C的DUMMY_FIFO=0仍保持，不错套SPI的dummy=1。

新`sweettouchdiag=1`限量记录原始128个非空事件、最多30次轮询统计，
含empty/error/ready/fault/skipped/outside/hid_calls以及IRQ/SDA/SCL。
hid_calls是送入桥函数的次数，**不是用户态收件或手势成功**。诊断默认
关闭，不加总线恢复、scan重发、reset、触摸firmware或电源试写。
当前修订尚未CI/真机验证；详细日志身份见`docs/gpu-acceptance.md`。

## 已验证与未验证

以下是历史基线：主屏幕、电源键和单次写黑熄屏已由用户确认，物理触摸曾确认可用；当前回归未恢复。
并有真实contact/HID事件作证；不是把模拟Home触发当成触摸成功。
独立FIFO测试已证明SE17 `FIFO_DISABLE=1`，不是一项可写软件开关。
原厂驱动在此条件下走GPI/GSI DMA，不能绕过它直接访问FIFO。

## 只读GPI/SMMU探测：run 41287180662

kernel SHA256 `ae5c07fc0874850247c8e09b2f478763600d58a9e0d48ed56e7d2910827dcc5c`。
boot SHA256 `8478f45e96dcdc38de3420262f3ccef1aac940aae6ab8a3d7f34c3c664ffbc47`。
日志 `gpi-probe-boot.log`：允许mask0xfa内 i1/i3/i4/i5/i6/i7 的channel0/1、
event context、ring、scratch均读为0；这与未配置一致，但不能只凭全零断言
所有权或固件可用。SMMU GR0 `cr0=0x2b0406 id0=0x4c017e5e id1=0x60000038
id2=0x5555`，94个SMR中无stream0x7b6匹配，clientpd=0且unmatched_fault=1。
不能直接用CPU物理地址提交GPI DMA。探测不改变SMMU或GPI寄存器。

继承GPIO状态：SDA55/SCL56/RESET12/IRQ122/VDDIO84均cfg=1（输入下拉）、io=0。
不能假定上电/复位已由固件保持，更不能把无响应全归因为传输引擎。
AVDD来自L1C 1.8V，现有RPMh helper仅针对sweet L11C，不能照搬3.2V到L1C。

## 独立GPIO I²C诊断路径

为了在不配置SMMU/GPI情况下验证芯片，增加显式opt-in `sweettouchgpio=1`。
这是一种使用TLMM GPIO的独立I²C主机，不是强行打开GENI FIFO。
前置：SE17为I²C且命令不活跃；只有本驱动线程访问引脚。

- GPIO55/56为SOUTH，12/84为NORTH。SDA/SCL latch固定为0，输出低或输入释放，
  从不推挽输出高；10µs半周期加MMIO开销，速度低于50kHz。
- 拉高VDDIO84、释放RESET12；不写PMIC电压，不上传/擦除触摸firmware。
- 组合5字节芯片ID命令+重复START+2字节读取，期望36:39。
- START前检查总线空闲；clock stretching有界等待；NACK、仲裁丢失和超时
  明确错误。仲裁/忙/超时仅释放总线，不发送恢复脉冲；终止时还原SDA/SCL配置。
- 成功读ID后才执行厂商active multitouch命令并读取事件FIFO。
- 协议纯测试覆盖重复START、ACK/NACK、256字节读取、超时、busy、仲裁及长度。
  测试通过不等于硬件成功；引脚拉高时间、AVDD状态、芯片firmware仍待真机证据。

此路径保留性能/熄屏修正，用`make-performance-boot.py --touch-gpio`打包。
DMA驱动仍未实现，GPIO若可用可先提供输入基线；不能承诺同等性能或多点流畅度。

### GPIO 首次测试（run 41288131588）

内核 `7546d325175e565deee607ce6d68666c51c4b294b56caee8ad110cddcdcb5048`，
boot `90958a53b1e7ee6d4cdafbff47b24cc7d8d86479aa99e07e0de0d96c18a7afb0`。
已临时启动，clock/GENI检查通过，GPIO释放后SDA=0、SCL=0，chip ID读取err60
（ETIMEDOUT），无36:39。驱动退出、系统/USB正常。不能据此判断芯片损坏。

进一步核对live FDT：ldoc1是`qcom,rpmh-xob-regulator`，固定1.8V；XOB仅接受
base+4 enable位，不能像sweet L11C的VRM发送电压/模式。RSC/DRV及TCS几何与
现有RPMh实现一致。0084在同一个受锁保护的RPMh transport内加入限定ldoc1的
单命令enable=1，保留空闲ACTIVE TCS检查、读回、完成确认及超时隔离；
不直接写SPMI电压，也不修改sleep/wake请求。成功ACK不是物理电压测量。

下一诊断版同时输出RESET/VDDIO以及SDA/SCL的配置回读；回读不符立即停止。
GPIO若报告EGPIO_PRESENT则按Linux设置EGPIO_ENABLE。电源投票为显式
`sweettouchgpio=1 sweettouchpower=1`，不默认对所有启动执行。

### 电源诊断版真机结果：run 41289150375

构建通过（含事件/I²C模拟/电源策略/性能测试、补丁及产物核验）。
kernel SHA256 `e9709ef19f08149deec339e55ff2ab6e001200494bf676f41bad57910e802252`；
boot `boot-cepheus-touch-power.img` SHA256
`c8eae5a0abd8dd9f8cd58f954497c0510c576b941b09bc0ef1efb2648718ea88`。
保留性能修复和PON-only电源键、单次Home，启用GPIO+ldoc1请求。
已临时启动（未重刷任何分区），日志 `touch-power-boot.log`：

```
SWEET RPMH: ldoc1 addr=0x43400 slave=4
CEPHEUS POWER: ldoc1 XOB enable only address=0x43404 data=1
SWEET RPMH: TCS0 done=1 ... cmd-status=0x10101/0/0
CEPHEUS POWER: ldoc1 enable request result=0
powered RESET cfg=0x2c3 io=0x3 VDDIO cfg=0x200 io=0x3
GPIO55 cfg=0x3 want=0x3 io=0x1
GPIO56 cfg=0x3 want=0x3 io=0x1
GPIO open-drain I2C SDA=1 SCL=1
probe 0x49 err=0 chip-id=36:39
active scan err=0
selected ST-FTS, 1080x2340, poll=10ms irq-hint=0
```

因此已验证L1C请求完成、GPIO回读正确、GPIO I²C芯片ID读取成功，以及
扫描开启命令传输成功。没有物理电压测量，不将命令ACK等同于实际触摸上报。
启动时USB ping正常，未见新panic。用户随后确认“触摸好了”。
当前输入使用GPIO开漏模拟I²C，GPI/GSI DMA仍未实现。

### 真实触摸确认（同一次启动）

`touch-power-boot.log` 共记录40条限流坐标日志、51次HID down与51次up，
其中有手指ID0/1/2和FromEdgeTip标记。示例：

```
SWEET HID: touch 0 down at 699,2399 edge
SWEET HID: touch 0 up at 802,1900 edge
```

该捕获窗口内GPIO bus/report错误0、SpringBoard crash标记0、kernel panic标记0。
这与用户实际操作反馈一起证实物理触摸链路工作，不证明所有多点手势/长时间稳定性。
确认后主机未再看到USB gadget，因此后续ping不通不能据此诊断为内核故障；
以上为已留存日志的结论，不声称后续仍在实时采集。

保留本版为可回退基线，不再改名覆盖原镜像、不重刷userdata。
后续事项：触摸延迟/高频移动/多点/熄屏后恢复测试、原生GPI DMA和音量加适配。
